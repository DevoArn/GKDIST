// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

// In-process UI test: renders the editor to PNG, simulates mouse interaction,
// checks hostile / corrupt state handling, and renders the application icon.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <cstdio>

using namespace gkd;

static int failures = 0;
static void expect (bool ok, const char* what)
{
    std::printf ("  %s: %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}

static void feed (GKDistProcessor& proc, int blocks, juce::Random& rng)
{
    juce::AudioBuffer<float> buf (4, 512);
    juce::MidiBuffer midi;
    static float b0 = 0, b1 = 0, b2 = 0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        buf.clear();
        for (int i = 0; i < 512; ++i)
        {
            const float w = rng.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
            const float pink = 0.07f * (b0 + b1 + b2 + w * 0.1848f);
            buf.setSample (0, i, pink);
            buf.setSample (1, i, pink * 0.9f);
        }
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (3);
    }
}

static void save (juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile (name);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("  wrote %s\n", f.getFullPathName().toRawUTF8());
}

static juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged, int clicks = 1,
                                    juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY, &c, &c, juce::Time::getCurrentTime(), down,
                             juce::Time::getCurrentTime(), clicks, dragged);
}

/** Press at a, drag to b in steps, release. */
static void dragOn (juce::Component& c, juce::Point<float> a, juce::Point<float> b)
{
    c.mouseDown (mouseEvent (c, a, a, false));
    for (int i = 1; i <= 8; ++i)
        c.mouseDrag (mouseEvent (c, a + (b - a) * ((float) i / 8.0f), a, true));
    c.mouseUp (mouseEvent (c, b, a, true));
}

/** Application icon: a sine wave driven into clipping. */
static void renderIcon (const juce::File& out)
{
    constexpr float S = 512.0f;
    juce::Image img (juce::Image::ARGB, (int) S, (int) S, true);
    juce::Graphics g (img);
    const juce::Rectangle<float> r (20.0f, 20.0f, S - 40.0f, S - 40.0f);
    juce::ColourGradient bg (juce::Colour (0xff2b2230), 0.0f, r.getY(), juce::Colour (0xff0b0e13), 0.0f, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 104.0f);
    g.setColour (juce::Colour (0x33ffffff));
    g.drawRoundedRectangle (r.reduced (2.0f), 102.0f, 4.0f);

    const float cy = S * 0.5f, amp = 150.0f, ceilY = 92.0f;
    g.setColour (juce::Colour (0x22ffffff));
    g.fillRect (60.0f, cy - ceilY - 2.0f, S - 120.0f, 4.0f);
    g.fillRect (60.0f, cy + ceilY - 2.0f, S - 120.0f, 4.0f);

    juce::Path clean, clipped;
    for (int i = 0; i <= 240; ++i)
    {
        const float t = i / 240.0f;
        const float x = 70.0f + t * (S - 140.0f);
        const float v = std::sin (t * 2.0f * juce::MathConstants<float>::pi * 1.5f);
        const float yc = cy - amp * v;
        const float yd = cy - ceilY * std::tanh (2.4f * v) / std::tanh (2.4f);
        if (i == 0) { clean.startNewSubPath (x, yc); clipped.startNewSubPath (x, yd); }
        else        { clean.lineTo (x, yc); clipped.lineTo (x, yd); }
    }
    g.setColour (juce::Colour (0x55ffffff));
    g.strokePath (clean, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0x66ffc04d));
    g.strokePath (clipped, juce::PathStrokeType (34.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xffffc04d));
    g.strokePath (clipped, juce::PathStrokeType (18.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("  wrote %s\n", out.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;

    // "--icon" re-renders Resources/icon.png (the committed application icon) and exits.
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--icon")
        {
            std::printf ("[icon]\n");
            renderIcon (juce::File::getCurrentWorkingDirectory().getChildFile ("Resources/icon.png"));
            return 0;
        }

    std::printf ("[state robustness]\n");
    {
        auto pp = std::make_unique<GKDistProcessor>();
        auto& p = *pp;
        p.setPlayConfigDetails (4, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        p.uiState.setProperty ("anaRange", 60, nullptr);
        p.uiState.setProperty ("width", 1600, nullptr);
        const int b = p.createBand (Shape::Low, 150.0, 18.0, 1.0, DistType::Fuzz);
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        p.uiState.setProperty ("anaRange", 90, nullptr);
        p.setParam (bandParamId (b, bpDrive), 3.0f);
        p.setStateInformation (saved.getData(), (int) saved.getSize());
        expect ((int) p.uiState["anaRange"] == 60 && (int) p.uiState["width"] == 1600, "UI settings survive save / reload");
        expect (std::abs (p.refs.readBand (b).driveDb - 18.0) < 0.01 && p.refs.readBand (b).type == DistType::Fuzz, "band parameters survive save / reload");

        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        if (auto* ui = xml->getChildByName ("UI"))
        {
            ui->setAttribute ("anaRange", 2000000000);
            ui->setAttribute ("anaTilt", "nan");
            ui->setAttribute ("width", -5);
            ui->setAttribute ("height", 1.0e30);
        }
        juce::MemoryBlock evil;
        juce::AudioProcessor::copyXmlToBinary (*xml, evil);
        p.setStateInformation (evil.getData(), (int) evil.getSize());
        expect ((int) p.uiState["anaRange"] == 120 && (int) p.uiState["width"] == 1280 && (int) p.uiState["height"] == 1600
                    && (double) p.uiState["anaTilt"] >= 0.0 && (double) p.uiState["anaTilt"] <= 6.0,
                "hostile UI values are clamped");
        {
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            juce::Image img (juce::Image::ARGB, e->getWidth(), e->getHeight(), true);
            juce::Graphics g (img);
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            e->paintEntireComponent (g, true);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::printf ("  editor paint with hostile state: %.1f ms\n", ms);
            expect (ms < 500.0, "editor paints quickly with hostile state");
        }
        juce::Random rng (99);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 400; ++i)
        {
            juce::MemoryBlock junk ((size_t) (9 + rng.nextInt (4000)));
            for (size_t k = 0; k < junk.getSize(); ++k) junk[k] = (char) rng.nextInt (256);
            if (i % 2 == 0)
            {
                const juce::uint32 magic = 0x21324356, len = (juce::uint32) junk.getSize() - 9;
                std::memcpy (junk.getData(), &magic, 4);
                std::memcpy (static_cast<char*> (junk.getData()) + 4, &len, 4);
            }
            p.setStateInformation (junk.getData(), (int) junk.getSize());
            buf.clear();
            p.processBlock (buf, midi);
        }
        expect (true, "survived 400 random / corrupt state blobs while processing");
    }

    std::printf ("[factory presets]\n");
    {
        auto pp = std::make_unique<GKDistProcessor>();
        auto& p = *pp;
        p.setPlayConfigDetails (4, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::Random rng (21);
        const auto names = presets::factoryNames();
        bool allOk = true;
        for (int i = 0; i < names.size(); ++i)
        {
            p.loadParameterState (presets::makeFactory (i, p.getDefaultParameterState()), false);
            juce::AudioBuffer<float> buf (4, 512);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            bool finite = true;
            for (int blk = 0; blk < 120; ++blk)
            {
                buf.clear();
                for (int k = 0; k < 512; ++k)
                {
                    const float v = 0.3f * std::sin (0.07f * (float) (blk * 512 + k)) + 0.05f * (rng.nextFloat() - 0.5f);
                    buf.setSample (0, k, v);
                    buf.setSample (1, k, v);
                }
                p.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < 512; ++k)
                    {
                        const float v = buf.getSample (c, k);
                        finite = finite && std::isfinite (v);
                        peak = std::max (peak, std::abs (v));
                    }
            }
            bool detOk = true;
            for (int b = 0; b < kNumBands; ++b)
            {
                const auto bp = p.refs.readBand (b);
                if (bp.used && bp.dynOn && bp.detMode == DetMode::Bell && i != 10)   // preset 10 moves its bell on purpose
                    detOk = detOk && std::abs (bp.detFreq / juce::jlimit (20.0, 20000.0, bp.freq) - 1.0) < 0.01;
            }
            std::printf ("  %-38s peak %.2f%s%s\n", names[i].toRawUTF8(), peak, finite ? "" : "  NON-FINITE", detOk ? "" : "  DETECTOR OFF-BAND");
            allOk = allOk && finite && peak < 8.0f && detOk;
        }
        expect (allOk, "every factory preset loads, processes finite audio and has its detector on the band");
    }

    std::printf ("[editor]\n");
    auto procPtr = std::make_unique<GKDistProcessor>();
    auto& proc = *procPtr;
    proc.uiState.setProperty ("gpu", false, nullptr);
    proc.setPlayConfigDetails (4, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.createBand (Shape::Low, 140.0, 14.0, 1.0, DistType::Tube);
    const int b2 = proc.createBand (Shape::Bell, 1200.0, 22.0, 1.4, DistType::Fuzz);
    proc.setParam (bandParamId (b2, bpDynOn), 1.0f);
    proc.setParam (bandParamId (b2, bpDynDrive), -12.0f);
    proc.setParam (bandParamId (b2, bpDynThresh), -40.0f);
    proc.setParam (bandParamId (b2, bpCompOn), 1.0f);
    proc.createBand (Shape::High, 7000.0, 10.0, 1.0, DistType::Foldback);
    const int b4 = proc.createBand (Shape::Bell, 3500.0, 30.0, 3.0, DistType::Bitcrush);
    proc.setParam (bandParamId (b4, bpWet), 40.0f);

    juce::Random rng (5);
    std::unique_ptr<GKDistEditor> ed (dynamic_cast<GKDistEditor*> (proc.createEditor()));
    ed->setSize (1280, 900);
    feed (proc, 200, rng);
    save (*ed, "build/ui_main.png");

    ed->getDisplay().selectBand (b2);
    feed (proc, 40, rng);
    expect (ed->getBandPanel().getBand() == b2, "selecting a node shows its band panel");
    save (*ed, "build/ui_band.png");

    std::printf ("[interaction]\n");
    {
        auto& d = ed->getDisplay();
        auto ev = [&] (juce::Point<float> pos, juce::Point<float> down, bool dragged, int clicks = 1)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                     juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                     juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                     juce::MouseInputSource::defaultTiltY, &d, &d, juce::Time::getCurrentTime(), down,
                                     juce::Time::getCurrentTime(), clicks, dragged);
        };
        // drag band 2 one octave up and 6 dB down in drive
        const auto start = d.nodePosition (b2);
        const juce::Point<float> target (d.freqToX (2400.0), d.driveToY (16.0));
        d.mouseDown (ev (start, start, false));
        d.mouseDrag (ev ((start + target) * 0.5f, start, true));
        d.mouseDrag (ev (target, start, true));
        d.mouseUp (ev (target, start, true));
        const auto p = proc.refs.readBand (b2);
        std::printf ("  dragged node: %.0f Hz, drive %.2f dB (expected 2400 Hz, 16 dB)\n", p.freq, p.driveDb);
        expect (std::abs (p.freq / 2400.0 - 1.0) < 0.01 && std::abs (p.driveDb - 16.0) < 0.1, "dragging a node sets frequency and drive");

        const int before = proc.findFreeBand();
        const juce::Point<float> empty (d.freqToX (400.0), d.driveToY (30.0));
        d.mouseDoubleClick (ev (empty, empty, false, 2));
        const auto nb = proc.refs.readBand (before);
        std::printf ("  double-click created band %d: %s, %.0f Hz, drive %.1f dB\n", before + 1,
                     nb.used ? "used" : "unused", nb.freq, nb.driveDb);
        expect (nb.used && std::abs (nb.freq / 400.0 - 1.0) < 0.01 && std::abs (nb.driveDb - 30.0) < 0.2,
                "double-click on empty space creates a band at that position");
        expect (d.getSelectedBand() == before && ed->getBandPanel().getBand() == before, "new band is selected and shown");
        proc.commitUndoPoint();
        proc.undo();
        expect (! proc.refs.readBand (before).used, "undo removes the created band");
    }

    std::printf ("[filter, detector and chain]\n");
    {
        auto& d = ed->getDisplay();
        d.selectBand (b2);
        proc.setParam (bandParamId (b2, bpFilterType), (float) (int) BandFilter::Peak);
        proc.setParam (bandParamId (b2, bpFilterFreq), 2000.0f);
        proc.setParam (bandParamId (b2, bpFilterGain), 0.0f);
        feed (proc, 16, rng);
        dragOn (d, d.filterHandlePosition (b2), { d.freqToX (4000.0), d.filterDbToY (9.0) });
        auto p = proc.refs.readBand (b2);
        std::printf ("  dragged filter handle: %.0f Hz, gain %+.2f dB (expected 4000 Hz, +9 dB)\n", p.filterFreq, p.filterGainDb);
        expect (std::abs (p.filterFreq / 4000.0 - 1.0) < 0.01 && std::abs (p.filterGainDb - 9.0) < 0.15, "filter handle sets filter frequency and gain");

        proc.setParam (bandParamId (b2, bpDetMode), (float) (int) DetMode::Bell);
        feed (proc, 16, rng);
        dragOn (d, d.detectorHandlePosition (b2), { d.freqToX (3000.0), d.specToY (-24.0f) });
        p = proc.refs.readBand (b2);
        std::printf ("  dragged detector handle: %.0f Hz, threshold %.2f dB (expected 3000 Hz, -24 dB)\n", p.detFreq, p.dynThresh);
        expect (std::abs (p.detFreq / 3000.0 - 1.0) < 0.01 && std::abs (p.dynThresh + 24.0) < 0.2, "detector handle sets bell frequency and threshold");
        expect (std::abs (p.freq / 2400.0 - 1.0) < 0.01, "detector bell moves independently of the band");

        // Drag FILTER (slot 0) onto slot 3 in the effect chain.
        auto& chain = ed->getBandPanel().getChainStrip();
        dragOn (chain, chain.slotCentre (0), chain.slotCentre (3));
        p = proc.refs.readBand (b2);
        juce::String order;
        for (int i = 0; i < kNumModules; ++i) order << moduleNames()[p.order[i]] << (i < kNumModules - 1 ? " > " : "");
        std::printf ("  chain after dragging FILTER to slot 4: %s\n", order.toRawUTF8());
        expect (p.order[0] == (int) Module::Distortion && p.order[3] == (int) Module::Filter && p.order[4] == (int) Module::Fx,
                "dragging a module reorders the band's chain");
        proc.setParam (bandParamId (b2, bpFxType), (float) (int) FxType::Phaser);
        feed (proc, 40, rng);
        save (*ed, "build/ui_band_fx.png");
        proc.undo();
        proc.undo();
        expect (proc.refs.readBand (b2).chainCode == 0 && proc.refs.readBand (b2).fxType == FxType::Off, "chain and FX changes are undoable");
        proc.setParam (bandParamId (b2, bpFxType), (float) (int) FxType::Phaser);
    }

    std::printf ("[intensity and clashes]\n");
    {
        auto& d = ed->getDisplay();
        const int hot1 = proc.createBand (Shape::Bell, 450.0, 40.0, 1.0, DistType::HardClip);
        const int hot2 = proc.createBand (Shape::Bell, 520.0, 38.0, 1.0, DistType::Fuzz);
        feed (proc, 80, rng);
        std::printf ("  intensity: band %d (40 dB) %.2f, band 1 (14 dB) %.2f; clash %d/%d %.2f, clash 1/%d %.2f\n",
                     hot1 + 1, d.bandIntensity (hot1), d.bandIntensity (0), hot1 + 1, hot2 + 1, d.clashBetween (hot1, hot2),
                     hot2 + 1, d.clashBetween (0, hot2));
        expect (d.bandIntensity (hot1) > d.bandIntensity (0) + 0.3f, "harder driven band is more intense");
        expect (d.clashBetween (hot1, hot2) > 0.3f, "two hot overlapping bands are reported as a clash");
        expect (d.clashBetween (b4, hot1) < 0.05f, "bands far apart do not clash");
        save (*ed, "build/ui_clash.png");
    }

    std::printf ("[mixer and modulation]\n");
    {
        int used = 0;
        for (int b = 0; b < kNumBands; ++b) used += proc.refs.readBand (b).used ? 1 : 0;
        ed->showPage (1);
        feed (proc, 30, rng);
        std::printf ("  mixer strips: %d for %d bands\n", ed->getMixerPanel().numVisibleStrips(), used);
        expect (ed->getMixerPanel().numVisibleStrips() == used, "one mixer strip per band");
        save (*ed, "build/ui_mixer.png");

        proc.setParam (modParamId (0, mpSource), (float) (int) ModSource::Lfo1);
        proc.setParam (modParamId (0, mpBand), (float) (b4 + 1));
        proc.setParam (modParamId (0, mpTarget), (float) (int) ModTarget::Drive);
        proc.setParam (modParamId (0, mpAmount), 40.0f);
        proc.setParam (globalParamId (gpLfo1Rate), 3.0f);
        proc.setParam (modParamId (1, mpSource), (float) (int) ModSource::InputEnv);
        proc.setParam (modParamId (1, mpTarget), (float) (int) ModTarget::Wet);
        proc.setParam (modParamId (1, mpAmount), -30.0f);
        ed->showPage (2);
        float lo = 1.0e9f, hi = -1.0e9f;
        for (int i = 0; i < 40; ++i)
        {
            feed (proc, 2, rng);
            const float v = proc.getTelemetry (b4).driveDb;
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        std::printf ("  band %d drive under LFO modulation: %.1f .. %.1f dB\n", b4 + 1, lo, hi);
        expect (hi - lo > 10.0f, "LFO modulation moves the band's drive");
        save (*ed, "build/ui_mod.png");
        ed->showPage (0);
    }

    std::printf ("[module pages and brickwall]\n");
    {
        auto& bp = ed->getBandPanel();
        ed->getDisplay().selectBand (b2);
        // a click (no drag) on a chain tile opens that module's page
        auto& chain = bp.getChainStrip();
        int fxSlot = 0;
        for (int s = 0; s < kNumModules; ++s) if (chain.moduleAt (s) == (int) Module::Fx) fxSlot = s;
        const auto c = chain.slotCentre (fxSlot);
        chain.mouseDown (mouseEvent (chain, c, c, false));
        chain.mouseUp (mouseEvent (chain, c, c, false));
        expect (bp.getModule() == Module::Fx, "clicking a chain tile opens that module");
        expect (proc.refs.readBand (b2).chainCode == encodeChainOrder (proc.refs.readBand (b2).order), "a click does not reorder the chain");
        feed (proc, 12, rng);
        save (*ed, "build/ui_page_fx.png");
        bp.showModule (Module::Filter);
        proc.setParam (bandParamId (b2, bpSlope), (float) kRegionBrickwall);
        feed (proc, 20, rng);
        expect (proc.refs.readBand (b2).slope == kRegionBrickwall, "band region slope can be set to brickwall");
        save (*ed, "build/ui_page_filter_brickwall.png");
        bp.showModule (Module::Distortion);
    }

    std::printf ("[waveform]\n");
    {
        ed->setWaveformVisible (true);
        auto& w = ed->getWaveform();
        expect (w.isVisible(), "waveform view can be shown");
        // Only the hot 450 Hz hard-clip band active: the waveform colour must come from it.
        int hot = -1;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto p = proc.refs.readBand (b);
            if (p.used && std::abs (p.freq - 450.0) < 1.0) hot = b;
        }
        std::vector<int> wasOn;
        for (int b = 0; b < kNumBands; ++b)
            if (b != hot && proc.refs.readBand (b).used && proc.refs.readBand (b).enabled)
            {
                wasOn.push_back (b);
                proc.setParam (bandParamId (b, bpEnabled), 0.0f);
            }
        for (int i = 0; i < 6; ++i) { w.pullNow(); feed (proc, 12, rng); }
        w.pullNow();
        float share = 0.0f;
        const auto plot = w.getLocalBounds().toFloat();
        const int dom = w.dominantBand (plot.getRight() - 20.0f, &share);
        std::printf ("  %d columns stored; newest columns dominated by band %d (%.0f %% of the change; expected band %d)\n",
                     w.columnsStored(), dom + 1, share * 100.0f, hot + 1);
        expect (w.columnsStored() > 100, "waveform receives columns from the audio thread");
        expect (dom == hot && share > 0.9f, "waveform colour is attributed to the band that changes the signal");
        save (*ed, "build/ui_waveform_single.png");
        for (int b : wasOn) proc.setParam (bandParamId (b, bpEnabled), 1.0f);
        for (int i = 0; i < 6; ++i) { w.pullNow(); feed (proc, 12, rng); }
        save (*ed, "build/ui_waveform.png");
    }

    std::printf ("[refresh rates]\n");
    {
        // Repaint scheduling at 60 / 144 / 240 Hz for a cheap and an expensive view.
        auto repaintsPerSecond = [] (double hz, double paintMs, double share)
        {
            ui::PaintBudget b (share);
            int painted = 0;
            for (int f = 0; f < (int) hz; ++f)
            {
                if (b.due (1.0 / hz)) { ++painted; b.record (paintMs); }
            }
            return painted;
        };
        const int cheap60 = repaintsPerSecond (60.0, 2.0, 0.45), cheap144 = repaintsPerSecond (144.0, 2.0, 0.45);
        const int heavy144 = repaintsPerSecond (144.0, 9.0, 0.45), heavy240 = repaintsPerSecond (240.0, 9.0, 0.45);
        std::printf ("  2 ms view: %d fps at 60 Hz, %d at 144 Hz;  9 ms view: %d fps at 144 Hz, %d at 240 Hz\n",
                     cheap60, cheap144, heavy144, heavy240);
        expect (cheap60 == 60 && cheap144 == 144, "cheap views repaint on every frame at any refresh rate");
        expect (heavy144 * 9.0 / 1000.0 <= 0.5 && heavy240 * 9.0 / 1000.0 <= 0.5 && heavy144 >= 40,
                "an expensive view stays within its share of the frame time");
    }

    std::printf ("[waveform speed]\n");
    {
        ed->setWaveformVisible (true);
        auto& w = ed->getWaveform();
        w.setSeconds (0.5);
        expect (std::abs ((double) proc.uiState["waveSeconds"] - 0.5) < 1.0e-9, "waveform speed can be set");
        const double before = w.getSeconds();
        const auto c = w.getLocalBounds().getCentre().toFloat();
        juce::MouseWheelDetails wd {};
        wd.deltaY = 0.5f;
        w.mouseWheelMove (mouseEvent (w, c, c, false, 1, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier)), wd);
        std::printf ("  Ctrl+wheel up: %.3f s -> %.3f s shown\n", before, w.getSeconds());
        expect (w.getSeconds() < before, "Ctrl+wheel makes the waveform faster");
        w.setSeconds (100.0);
        expect (std::abs (w.getSeconds() - 20.0) < 1.0e-9, "waveform speed is clamped (20 s max)");
        w.setSeconds (2.0);
        // the view advances smoothly while audio runs
        feed (proc, 10, rng);
        const double h0 = w.getHead();
        double prev = h0;
        bool monotonic = true;
        for (int i = 0; i < 10; ++i)
        {
            feed (proc, 2, rng);
            w.pullNow();
            monotonic = monotonic && w.getHead() >= prev;
            prev = w.getHead();
        }
        std::printf ("  scroll position advanced %.0f columns over 10 frames (monotonic: %s)\n", prev - h0, monotonic ? "yes" : "NO");
        expect (monotonic && prev > h0, "waveform scrolls forward continuously");
    }

    std::printf ("[OTT compressor]\n");
    {
        auto& d = ed->getDisplay();
        proc.setParam (globalParamId (gpCompOn), 1.0f);
        proc.setParam (globalParamId (gpCompMode), 1.0f);
        feed (proc, 30, rng);
        const auto tab0 = d.crossoverTab (0), tab1 = d.crossoverTab (1);
        expect (! tab0.isEmpty() && ! tab1.isEmpty(), "OTT crossover tabs are shown on the display");
        dragOn (d, tab0.getCentre(), { d.freqToX (150.0), tab0.getCentreY() });
        dragOn (d, tab1.getCentre(), { d.freqToX (4000.0), tab1.getCentreY() });
        const auto g = proc.refs.readGlobal();
        std::printf ("  dragged crossovers: %.1f Hz, %.0f Hz (expected 150, 4000)\n", g.ottLowX, g.ottHighX);
        expect (std::abs (g.ottLowX / 150.0 - 1.0) < 0.02 && std::abs (g.ottHighX / 4000.0 - 1.0) < 0.02,
                "dragging the tabs sets the OTT crossovers");
        feed (proc, 60, rng);
        const float g0 = proc.getOttBandGain (0), g1 = proc.getOttBandGain (1), g2 = proc.getOttBandGain (2);
        std::printf ("  live OTT band gains: low %+.1f, mid %+.1f, high %+.1f dB\n", g0, g1, g2);
        expect (std::isfinite (g0) && std::isfinite (g1) && std::isfinite (g2) && (g0 != 0.0f || g1 != 0.0f || g2 != 0.0f),
                "OTT band gains reach the meter");
        save (*ed, "build/ui_ott.png");
        proc.setParam (globalParamId (gpCompMode), 0.0f);
        feed (proc, 10, rng);
        expect (d.crossoverTab (0).isEmpty(), "crossover tabs disappear in standard mode");
        proc.setParam (globalParamId (gpCompOn), 0.0f);
    }

    std::printf ("[phase distortion]\n");
    {
        ed->getDisplay().selectBand (b2);
        ed->getBandPanel().showModule (Module::Distortion);
        proc.setParam (bandParamId (b2, bpType), (float) (int) DistType::PhaseDist);
        proc.setParam (bandParamId (b2, bpDrive), 30.0f);
        feed (proc, 30, rng);
        expect (proc.refs.readBand (b2).type == DistType::PhaseDist, "phase distortion can be selected");
        save (*ed, "build/ui_phasedist.png");
    }

    std::printf ("[colour themes]\n");
    {
        const int sel = ed->getDisplay().getSelectedBand();
        const auto names = ui::themeNames();
        for (int t = 0; t < (int) ThemeId::Count; ++t)
        {
            ed->setTheme ((ThemeId) t, false);
            feed (proc, 12, rng);   // the editor rebuilds asynchronously
            const bool ok = ui::currentTheme() == (ThemeId) t && ed->getDisplay().getSelectedBand() == sel
                            && ed->getWaveform().isVisible();
            std::printf ("  %-16s bg %s, band 1 %s%s\n", names[t].toRawUTF8(), ui::Colours::bg0.toDisplayString (false).toRawUTF8(),
                         ui::bandColour (0).toDisplayString (false).toRawUTF8(), ok ? "" : "  STATE LOST");
            expect (ok, "theme switch rebuilds the UI and keeps the selection and views");
            save (*ed, "build/ui_theme_" + juce::String (t) + ".png");
        }
        ed->setTheme (ThemeId::Ember, false);
        feed (proc, 6, rng);
        ed->setWaveformVisible (false);
    }

    proc.setParam (globalParamId (gpLimitOn), 1.0f);
    ed->setSize (1600, 1040);
    ed->getDisplay().selectBand (b4);
    feed (proc, 60, rng);
    save (*ed, "build/ui_large.png");

    {
        // Per-frame paint cost of the animated views (software renderer, the slowest path).
        ed->setWaveformVisible (true);
        feed (proc, 20, rng);
        auto timePaint = [] (juce::Component& c) -> double
        {
            juce::Image img (juce::Image::ARGB, juce::jmax (1, c.getWidth()), juce::jmax (1, c.getHeight()), true);
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 20; ++i) { juce::Graphics g (img); c.paintEntireComponent (g, true); }
            return (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0;
        };
        auto& disp = ed->getDisplay();
        disp.paintProfile = {};
        disp.paintProfile.on = true;
        const double dMs = timePaint (disp), wMs = timePaint (ed->getWaveform());
        const auto& pp = disp.paintProfile;
        std::printf ("  display stages (ms/frame): background %.2f, spectra %.2f, bands %.2f, handles %.2f, nodes %.2f, read-out %.2f\n",
                     pp.background / 20.0, pp.spectra / 20.0, pp.bands / 20.0, pp.extras / 20.0, pp.nodes / 20.0, pp.readout / 20.0);
        std::printf ("    bands: mixed fill %.2f, clashes %.2f, outlines %.2f\n", pp.mixFill / 20.0, pp.clash / 20.0, pp.outlines / 20.0);
        disp.paintProfile.on = false;
        std::printf ("  per frame at 1600x1040: spectrum display %.1f ms, waveform %.1f ms (software)\n", dMs, wMs);
        ed->setWaveformVisible (false);
    }
    {
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) { juce::Graphics g (img); ed->paintEntireComponent (g, true); }
        std::printf ("  software repaint 1600x1040: %.1f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
    }

    ed.reset();
    proc.releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "UI TEST PASSED" : "UI TEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
