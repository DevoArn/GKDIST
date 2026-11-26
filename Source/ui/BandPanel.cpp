// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BandPanel.h"

namespace gkd::ui
{

namespace
{
    constexpr int kKnobW = 56, kKnobH = 72;          // region / output / dynamics
    constexpr int kPageKnobW = 64, kPageKnobH = 84;  // module pages
    constexpr int kColW = 96, kPageColW = 110, kGap = 10;
    constexpr int kChipW = 24, kChipGap = 2;

    const char* moduleTitle (Module m)
    {
        static const char* t[] = { "Filter", "Drive", "Phat", "Noise", "FX", "Compressor", "Clip / Limit" };
        return t[juce::jlimit (0, kNumModules - 1, (int) m)];
    }

    const char* moduleHelp (Module m)
    {
        switch (m)
        {
            case Module::Filter:     return "Shapes the band at its place in the chain. Drag the F handle on the display.";
            case Module::Distortion: return "The band's distortion. Bias adds even harmonics, Auto Gain keeps the level steady. "
                                            "Phase Dist warps the timing instead of the level (Depth, Tone).";
            case Module::Phat:       return "Adds a sub-octave and warm low end, generated from the band's lows.";
            case Module::Noise:      return "Noise shaped to the band's region. Follow makes it breathe with the signal.";
            case Module::Fx:         return "Modulation effect: phaser, flanger, chorus, vibrato, tremolo or auto pan.";
            case Module::Compressor: return "Compresses the band at this point of the chain.";
            case Module::Clipper:    return "Soft clip, hard clip or limit the band at the ceiling.";
            case Module::Count:
            default:                 return "";
        }
    }

    void section (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name)
    {
        g.setColour (Colours::bg1.withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.setColour (Colours::textDim);
        g.setFont (LookAndFeel::font (10.5f, true));
        g.drawText (name, r.withHeight (14).translated (9, 3), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------------------
void BandChips::tick (double dt)
{
    const bool live = proc.isAudioRunning();
    bool changed = false;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto p = proc.refs.readBand (b);
        changed = changed || p.used != bands[b].used || p.enabled != bands[b].enabled;
        bands[b] = p;
        const auto t = proc.getTelemetry (b);
        const float drive = live ? t.driveDb : (float) p.driveDb, wet = live ? t.wet : (float) p.wet;
        const float target = juce::jlimit (0.0f, 1.0f, drive / 36.0f) * (0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, wet));
        const float v = smoothTowards (intensity[b], target, dt, 0.03);
        changed = changed || std::abs (v - intensity[b]) > 0.002f;
        intensity[b] = v;
    }
    if (changed) repaint();
}

int BandChips::chipAt (float x) const
{
    const int i = (int) std::floor (x / (float) (kChipW + kChipGap));
    return i >= 0 && i < kNumBands ? i : -1;
}

void BandChips::paint (juce::Graphics& g)
{
    g.setFont (LookAndFeel::font (10.0f, true));
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto r = juce::Rectangle<float> ((float) (b * (kChipW + kChipGap)), 1.0f, (float) kChipW, (float) getHeight() - 2.0f);
        if (bands[b].used)
        {
            const auto c = intensityColour (b, intensity[b]);
            g.setColour (c.withAlpha (bands[b].enabled ? 0.9f : 0.35f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
        }
        else
        {
            g.setColour (Colours::panelEdge.withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Colours::textDim.withAlpha (0.45f));
        }
        g.drawText (juce::String (b + 1), r, juce::Justification::centred);
        if (b == selected)
        {
            g.setColour (Colours::text);
            g.drawRoundedRectangle (r.expanded (1.0f), 5.0f, 1.8f);
        }
    }
}

void BandChips::mouseDown (const juce::MouseEvent& e)
{
    const int b = chipAt (e.position.x);
    if (b >= 0 && bands[b].used && onSelect) onSelect (b);
}

juce::String BandChips::getTooltip()
{
    if (hover < 0) return {};
    const auto& p = bands[hover];
    if (! p.used) return "Band " + juce::String (hover + 1) + ": unused (double-click the display to add a band)";
    return "Band " + juce::String (hover + 1) + ": " + shapeNames()[(int) p.shape] + " " + formatFrequency (p.freq) + ", "
         + distTypeNames()[(int) p.type] + " " + formatDb (p.driveDb, false);
}

// ---------------------------------------------------------------------------
BandPanel::BandPanel (GKDistProcessor& p) : proc (p), chain (p), chips (p)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &deleteBtn, &enableBtn, &soloBtn, &muteBtn, &autoBtn, &compBtn, &dynBtn, &scBtn,
             &shapeBox, &slopeBox, &typeBox, &filterBox, &filterSlopeBox, &noiseTypeBox, &clipBox, &fxBox, &detBox,
             &freq, &q, &drive, &bias, &fFreq, &fRes, &fGain, &wet, &gain, &pan, &fxRate, &fxDepth, &fxFeedback, &fxMix,
             &phat, &noise, &follow, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup, &ceiling,
             &dThresh, &dAttack, &dRelease, &detFreq, &detQ, &dGain, &dDrive, &dWet, &curve, &response, &grMeter, &dynMeter,
             &chain, &chips })
        addAndMakeVisible (c);

    pageControls[(size_t) Module::Filter]     = { &filterBox, &filterSlopeBox, &fFreq, &fRes, &fGain, &response };
    pageControls[(size_t) Module::Distortion] = { &typeBox, &autoBtn, &drive, &bias, &curve };
    pageControls[(size_t) Module::Phat]       = { &phat };
    pageControls[(size_t) Module::Noise]      = { &noiseTypeBox, &noise, &follow };
    pageControls[(size_t) Module::Fx]         = { &fxBox, &fxRate, &fxDepth, &fxFeedback, &fxMix };
    pageControls[(size_t) Module::Compressor] = { &compBtn, &grMeter, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup };
    pageControls[(size_t) Module::Clipper]    = { &clipBox, &ceiling };

    for (auto* k : { &freq, &q, &drive, &bias, &fFreq, &fRes, &fGain, &wet, &gain, &pan, &fxRate, &fxDepth, &fxFeedback, &fxMix,
                     &phat, &noise, &follow, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup, &ceiling,
                     &dThresh, &dAttack, &dRelease, &detFreq, &detQ, &dGain, &dDrive, &dWet })
        k->onEdit = [this] { edited(); };
    for (auto* t : { &enableBtn, &muteBtn, &autoBtn, &compBtn, &dynBtn, &scBtn })
        t->onEdit = [this] { refresh(); edited(); };
    soloBtn.onEdit = [this]
    {
        if (band >= 0 && soloBtn.getToggleState())
            for (int o = 0; o < kNumBands; ++o)
                if (o != band && proc.refs.readBand (o).solo) proc.setParam (bandParamId (o, bpSolo), 0.0f);
        edited();
    };
    for (auto* c : { &shapeBox, &slopeBox, &typeBox, &filterBox, &filterSlopeBox, &noiseTypeBox, &clipBox, &fxBox, &detBox })
        c->onEdit = [this] { refresh(); edited(); };
    chain.onEdit = [this] { edited(); };
    chain.onModuleSelected = [this] (Module m) { showModule (m); };
    chips.onSelect = [this] (int b) { if (onSelectBand) onSelectBand (b); };

    enableBtn.setOnColour (Colours::on.withAlpha (0.35f));
    soloBtn.setOnColour (Colours::warn.withAlpha (0.45f));
    muteBtn.setOnColour (Colours::collision.withAlpha (0.45f));
    autoBtn.setTooltip ("Auto gain: keeps the level roughly constant while you change the drive");
    muteBtn.setTooltip ("Mute: removes this band's frequency region from the output");
    dynBtn.setTooltip ("Dynamics: the detector level moves gain, drive and wet by their +/- ranges");
    scBtn.setTooltip ("Trigger the dynamics from the external side-chain input");
    detBox.setTooltip ("Detector: listen to the band's own region, or to an independent bell (drag the T handle on the display)");
    slopeBox.setTooltip ("Region slope; Brickwall = 144 dB/oct edges (a flat band between the edges for Bell)");
    filterBox.setTooltip ("Band filter; its place in the signal chain is set in the effect chain above");
    fxBox.setTooltip ("Modulation effect for this band");
    phat.slider.setTooltip ("Phat: sub-octave and saturated low end for weight");
    follow.slider.setTooltip ("Noise follows the band level (100 % = silent when the band is silent)");
    curve.setTooltip ("Transfer curve of the distortion (input -> output)");
    response.setTooltip ("Filter response");
    detFreq.slider.setTooltip ("Frequency of the detector bell");
    detQ.slider.setTooltip ("Width of the detector bell");
    deleteBtn.onClick = [this]
    {
        if (band < 0) return;
        const int b = band;
        setBand (-1);
        proc.deleteBand (b);
        edited();
        if (onSelectBand) onSelectBand (-1);
    };

    module = (Module) juce::jlimit (0, kNumModules - 1, (int) proc.uiState.getProperty ("bandModule", (int) Module::Distortion));
    chain.setSelectedModule (module);
    setBand (-1);
    startTimerHz (20);
}

BandPanel::~BandPanel() { stopTimer(); }

void BandPanel::edited()
{
    juce::Component::SafePointer<BandPanel> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
}

void BandPanel::showModule (Module m)
{
    module = (Module) juce::jlimit (0, kNumModules - 1, (int) m);
    chain.setSelectedModule (module);
    proc.uiState.setProperty ("bandModule", (int) module, nullptr);
    refresh();
    repaint();
}

void BandPanel::setBand (int b)
{
    band = b;
    chips.setSelected (b);
    chips.tick();   // a freshly built panel shows the bands at once
    liveBand = -1;
    auto prm = [this] (int idx) { return band >= 0 ? proc.param (bandParamId (band, idx)) : nullptr; };

    freq.attach (prm (bpFreq)); q.attach (prm (bpQ)); drive.attach (prm (bpDrive)); bias.attach (prm (bpBias));
    fFreq.attach (prm (bpFilterFreq)); fRes.attach (prm (bpFilterRes)); fGain.attach (prm (bpFilterGain));
    wet.attach (prm (bpWet)); gain.attach (prm (bpGain)); pan.attach (prm (bpPan));
    fxRate.attach (prm (bpFxRate)); fxDepth.attach (prm (bpFxDepth)); fxFeedback.attach (prm (bpFxFeedback)); fxMix.attach (prm (bpFxMix));
    phat.attach (prm (bpPhat)); noise.attach (prm (bpNoiseLevel)); follow.attach (prm (bpNoiseFollow));
    cThresh.attach (prm (bpCompThresh)); cRatio.attach (prm (bpCompRatio)); cAttack.attach (prm (bpCompAttack));
    cRelease.attach (prm (bpCompRelease)); cMakeup.attach (prm (bpCompMakeup)); ceiling.attach (prm (bpClipCeiling));
    dThresh.attach (prm (bpDynThresh)); dAttack.attach (prm (bpDynAttack)); dRelease.attach (prm (bpDynRelease));
    detFreq.attach (prm (bpDetFreq)); detQ.attach (prm (bpDetQ));
    dGain.attach (prm (bpDynGain)); dDrive.attach (prm (bpDynDrive)); dWet.attach (prm (bpDynWet));
    enableBtn.attach (prm (bpEnabled)); soloBtn.attach (prm (bpSolo)); muteBtn.attach (prm (bpMute)); autoBtn.attach (prm (bpAutoGain));
    compBtn.attach (prm (bpCompOn)); dynBtn.attach (prm (bpDynOn)); scBtn.attach (prm (bpDynSC));
    shapeBox.attach (prm (bpShape)); slopeBox.attach (prm (bpSlope)); typeBox.attach (prm (bpType));
    filterBox.attach (prm (bpFilterType)); filterSlopeBox.attach (prm (bpFilterSlope)); noiseTypeBox.attach (prm (bpNoiseType));
    clipBox.attach (prm (bpClipMode)); fxBox.attach (prm (bpFxType)); detBox.attach (prm (bpDetMode));
    chain.setBand (band);

    const bool has = band >= 0;
    for (auto* c : getChildren()) c->setVisible (has);
    chips.setVisible (true);   // band selection is always available
    if (has)
    {
        const auto col = bandColour (band);
        for (auto* k : { &freq, &q, &drive, &bias, &fFreq, &fRes, &fGain, &wet, &gain, &pan, &fxRate, &fxDepth, &fxFeedback, &fxMix,
                         &phat, &noise, &follow, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup, &ceiling,
                         &dThresh, &dAttack, &dRelease, &detFreq, &detQ, &dGain, &dDrive, &dWet })
            k->setAccent (col);
        dynBtn.setOnColour (col.withAlpha (0.45f));
        compBtn.setOnColour (col.withAlpha (0.45f));
        refresh();
    }
    repaint();
}

void BandPanel::refresh()
{
    if (band < 0) return;
    const auto p = proc.refs.readBand (band);
    if (! p.used) { if (onSelectBand) onSelectBand (-1); return; }

    // Only the selected module's page is visible.
    for (int m = 0; m < kNumModules; ++m)
        for (auto* c : pageControls[(size_t) m]) c->setVisible ((Module) m == module);

    q.setEnabled (p.shape == Shape::Bell);
    slopeBox.setEnabled (p.shape != Shape::Full);
    freq.setEnabled (p.shape != Shape::Full);
    const bool crush = p.type == DistType::Bitcrush, phase = p.type == DistType::PhaseDist;
    bias.setCaption (crush ? "RATE" : (phase ? "TONE" : "BIAS"));
    drive.setCaption (crush ? "CRUSH" : (phase ? "DEPTH" : "DRIVE"));
    autoBtn.setEnabled (! crush && ! phase);

    const bool filterOn = p.filterType != BandFilter::Off;
    for (auto* c : std::initializer_list<juce::Component*> { &filterSlopeBox, &fFreq, &fRes }) c->setEnabled (filterOn);
    fGain.setEnabled (filterOn && filterHasGain (p.filterType));

    const bool fxOn = p.fxType != FxType::Off;
    for (auto* c : { &fxRate, &fxDepth, &fxMix }) c->setEnabled (fxOn);
    fxFeedback.setEnabled (p.fxType == FxType::Phaser || p.fxType == FxType::Flanger || p.fxType == FxType::Chorus);

    const bool noiseOn = p.noiseDb > kNoiseOffDb + 0.05;
    noiseTypeBox.setEnabled (noiseOn);
    follow.setEnabled (noiseOn);
    for (auto* c : { &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup }) c->setEnabled (p.compOn);
    ceiling.setEnabled (p.clipMode != ClipMode::Off);
    for (auto* c : { &dThresh, &dAttack, &dRelease, &dGain, &dDrive, &dWet }) c->setEnabled (p.dynOn);
    scBtn.setEnabled (p.dynOn);
    detBox.setEnabled (p.dynOn);
    detFreq.setEnabled (p.dynOn && p.detMode == DetMode::Bell);
    detQ.setEnabled (p.dynOn && p.detMode == DetMode::Bell);
    pan.setEnabled (proc.getMainBusNumOutputChannels() > 1);
    chain.update (p);
}

void BandPanel::timerCallback()
{
    if (band < 0) return;
    refresh();
}

void BandPanel::frame (double dt)
{
    chips.tick (dt);
    if (band < 0 || ! isVisible()) return;
    const auto p = proc.refs.readBand (band);
    if (! p.used) return;

    // Curves follow the band as it moves with dynamics and modulation.
    const bool live = proc.isAudioRunning();
    const auto t = proc.getTelemetry (band);
    const float tDrive = live ? juce::jlimit (0.0f, 60.0f, t.driveDb) : (float) p.driveDb;
    const float tBias = live ? juce::jlimit (-1.0f, 1.0f, t.bias) : (float) p.bias;
    const float tLogF = (float) std::log (live ? juce::jlimit (20.0, 20000.0, (double) t.filterFreq) : p.filterFreq);
    const float tLogR = (float) std::log (live ? juce::jlimit (0.1, 10.0, (double) t.filterRes) : p.filterRes);
    const float tGain = live ? juce::jlimit (-24.0f, 24.0f, t.filterGainDb) : (float) p.filterGainDb;
    if (! live || liveBand != band)
    {
        liveDrive = tDrive; liveBias = tBias; liveFiltLogF = tLogF; liveFiltLogR = tLogR; liveFiltGain = tGain;
        liveBand = band;
    }
    else
    {
        constexpr double tau = 0.012;
        liveDrive = smoothTowards (liveDrive, tDrive, dt, tau);
        liveBias = smoothTowards (liveBias, tBias, dt, tau);
        liveFiltLogF = smoothTowards (liveFiltLogF, tLogF, dt, tau);
        liveFiltLogR = smoothTowards (liveFiltLogR, tLogR, dt, tau);
        liveFiltGain = smoothTowards (liveFiltGain, tGain, dt, tau);
    }
    // rounding: a redraw only when the curve visibly changes
    curve.set (p.type, std::round (liveDrive * 20.0f) / 20.0, std::round (liveBias * 500.0f) / 500.0, p.autoGain, bandColour (band));
    auto lp = p;
    lp.filterFreq = std::exp ((double) std::round (liveFiltLogF * 2000.0f) / 2000.0);
    lp.filterRes = std::exp ((double) std::round (liveFiltLogR * 1000.0f) / 1000.0);
    lp.filterGainDb = std::round (liveFiltGain * 50.0f) / 50.0;
    response.set (lp, proc.getCurrentSampleRate(), bandColour (band));

    const float gr = proc.getCompGr (band);
    grMeter.setValue (gr / 24.0f, "GR " + juce::String (gr, 1) + " dB");
    const float a = proc.getDynAmount (band);
    dynMeter.setColour (bandColour (band));
    dynMeter.setValue (a, "Amount " + formatNumber (a * 100.0f, 0) + " %");
}

void BandPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);

    if (band < 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (LookAndFeel::font (15.0f));
        g.drawText ("Double-click the display to add a band, or pick a band above.",
                    getLocalBounds().withTrimmedTop (kHeader), juce::Justification::centred);
        return;
    }

    const auto col = bandColour (band);
    g.setColour (col);
    g.fillRect (0, kHeader - 2, getWidth() - kTabsWidth, 2);
    g.setFont (LookAndFeel::font (15.0f, true));
    g.setColour (Colours::text);
    g.drawText ("Band " + juce::String (band + 1), 12, 3, 80, 20, juce::Justification::centredLeft);
    const auto p = proc.refs.readBand (band);
    g.setColour (col);
    g.strokePath (shapeIcon (p.shape, { 80.0f, 7.0f, 24.0f, 12.0f }), juce::PathStrokeType (1.6f));

    section (g, secRegion, "REGION");
    section (g, secOut, "OUTPUT");
    section (g, secDyn, "DYNAMICS");
    section (g, secChain, "EFFECT CHAIN");

    g.setFont (LookAndFeel::font (10.5f));
    g.setColour (Colours::textDim);
    g.drawText ("click a module to edit it  -  drag to reorder", secChain.withHeight (14).translated (0, 3).withTrimmedRight (10),
                juce::Justification::centredRight);

    // module page header: title in the band colour + what it does
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (pageArea.getY() - 4, (float) pageArea.getX(), (float) pageArea.getRight());
    g.setFont (LookAndFeel::font (14.0f, true));
    g.setColour (col);
    const juce::String title (moduleTitle (module));
    g.drawText (title, pageArea.getX(), pageArea.getY(), 120, 18, juce::Justification::centredLeft);
    g.setFont (LookAndFeel::font (11.0f));
    g.setColour (Colours::textDim);
    const int tw = LookAndFeel::font (14.0f, true).getStringWidth (title) + 12;
    g.drawFittedText (moduleHelp (module), pageArea.getX() + tw, pageArea.getY() + 1, pageArea.getWidth() - tw, 34,
                      juce::Justification::topLeft, 2, 1.0f);
    if (! ChainStrip::moduleActive (module, p))
    {
        g.setColour (Colours::textDim.withAlpha (0.7f));
        g.drawText ("(off)", pageArea.getX(), pageArea.getY() + 20, 120, 14, juce::Justification::centredLeft);
    }

    g.setFont (LookAndFeel::font (10.5f));
    g.setColour (Colours::textDim);
    g.drawFittedText (p.detMode == DetMode::Bell ? "Detector bell: drag T on the display (wheel: width)."
                                                 : "Detector: the band's own region (drag T for the threshold).",
                      secDyn.withTrimmedTop (secDyn.getHeight() - 26).reduced (10, 4), juce::Justification::centredLeft, 2);
}

void BandPanel::layoutModulePage()
{
    auto a = pageArea.withTrimmedTop (36);
    auto knobs = [&a] (std::initializer_list<Knob*> list)
    {
        for (auto* k : list) { k->setBounds (a.removeFromLeft (kPageKnobW).withHeight (kPageKnobH)); a.removeFromLeft (2); }
    };
    auto combos = [&a] (std::initializer_list<juce::Component*> list)
    {
        auto col = a.removeFromLeft (kPageColW);
        a.removeFromLeft (10);
        int y = col.getY() + 6;
        for (auto* c : list) { c->setBounds (col.getX(), y, col.getWidth(), 22); y += 27; }
    };

    combos ({ &filterBox, &filterSlopeBox }); knobs ({ &fFreq, &fRes, &fGain });
    a.removeFromLeft (8);
    response.setBounds (a.withHeight (std::min (a.getHeight(), 140)));

    a = pageArea.withTrimmedTop (36);
    combos ({ &typeBox, &autoBtn }); knobs ({ &drive, &bias });
    a.removeFromLeft (8);
    curve.setBounds (a.removeFromLeft (170).withHeight (std::min (a.getHeight(), 140)));

    a = pageArea.withTrimmedTop (36);
    knobs ({ &phat });

    a = pageArea.withTrimmedTop (36);
    combos ({ &noiseTypeBox }); knobs ({ &noise, &follow });

    a = pageArea.withTrimmedTop (36);
    combos ({ &fxBox }); knobs ({ &fxRate, &fxDepth, &fxFeedback, &fxMix });

    a = pageArea.withTrimmedTop (36);
    combos ({ &compBtn, &grMeter }); knobs ({ &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup });

    a = pageArea.withTrimmedTop (36);
    combos ({ &clipBox }); knobs ({ &ceiling });
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto head = r.removeFromTop (kHeader).withTrimmedRight (kTabsWidth);
    head.removeFromLeft (112);
    enableBtn.setBounds (head.removeFromLeft (44).reduced (0, 3));
    head.removeFromLeft (4);
    soloBtn.setBounds (head.removeFromLeft (52).reduced (0, 3));
    head.removeFromLeft (4);
    deleteBtn.setBounds (head.removeFromLeft (44).reduced (0, 3));
    head.removeFromLeft (16);
    chips.setBounds (head.removeFromLeft (kNumBands * (kChipW + kChipGap)).reduced (0, 3));

    auto body = r.withTrimmedTop (4).withTrimmedBottom (6);
    auto colA = body.removeFromLeft (280);
    body.removeFromLeft (kGap);
    secDyn = body.removeFromRight (336);
    body.removeFromRight (kGap);
    secChain = body;
    secRegion = colA.removeFromTop ((colA.getHeight() - 8) / 2);
    colA.removeFromTop (8);
    secOut = colA;

    auto inner = [] (juce::Rectangle<int> s) { return s.withTrimmedTop (20).reduced (8, 2); };
    auto knobs = [] (juce::Rectangle<int>& area, std::initializer_list<Knob*> list)
    {
        for (auto* k : list) k->setBounds (area.removeFromLeft (kKnobW).withHeight (kKnobH));
    };
    auto combos = [] (juce::Rectangle<int>& area, int width, std::initializer_list<juce::Component*> list)
    {
        auto col = area.removeFromLeft (width);
        area.removeFromLeft (4);
        int y = col.getY() + 4;
        for (auto* c : list) { c->setBounds (col.getX(), y, col.getWidth(), 20); y += 23; }
    };

    auto s = inner (secRegion); combos (s, kColW, { &shapeBox, &slopeBox }); knobs (s, { &freq, &q });
    s = inner (secOut);         combos (s, 72, { &muteBtn }); knobs (s, { &wet, &gain, &pan });

    s = inner (secDyn);
    combos (s, kColW, { &dynBtn, &scBtn, &detBox, &dynMeter });
    auto row1 = s.removeFromTop (kKnobH);
    s.removeFromTop (8);
    auto row2 = s.removeFromTop (kKnobH);
    knobs (row1, { &detFreq, &detQ, &dThresh, &dAttack });
    knobs (row2, { &dRelease, &dGain, &dDrive, &dWet });

    chain.setBounds (secChain.withTrimmedTop (22).withHeight (46).reduced (10, 0));
    pageArea = secChain.withTrimmedTop (22 + 46 + 12).reduced (10, 0).withTrimmedBottom (6);
    layoutModulePage();
}

} // namespace gkd::ui
