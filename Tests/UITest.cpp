// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

// In-process UI test: renders the editor to PNG, simulates mouse interaction,
// checks hostile / corrupt state handling, and renders the application icon.
#include "PluginProcessor.h"
#include "PluginEditor.h"
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
    ed->setSize (1280, 860);
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

    proc.setParam (globalParamId (gpLimitOn), 1.0f);
    ed->setSize (1600, 1000);
    ed->getDisplay().selectBand (b4);
    feed (proc, 60, rng);
    save (*ed, "build/ui_large.png");

    {
        juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) { juce::Graphics g (img); ed->paintEntireComponent (g, true); }
        std::printf ("  software repaint 1600x1000: %.1f ms per frame\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
    }

    ed.reset();
    proc.releaseResources();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "UI TEST PASSED" : "UI TEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
