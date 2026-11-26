// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BandPanel.h"

namespace gkd::ui
{

namespace
{
    // Widest row (row 2) needs 1206 px: fits the 1280 px minimum window width.
    constexpr int kKnobW = 56, kKnobH = 72, kColW = 96, kGap = 10;
    const char* kSectionNames[] = { "REGION", "DISTORTION", "FILTER", "OUTPUT", "FX", "PHAT / NOISE", "COMPRESSOR",
                                    "CLIP / LIMIT", "DYNAMICS", "EFFECT CHAIN" };
}

BandPanel::BandPanel (GKDistProcessor& p) : proc (p), chain (p)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &prevBtn, &nextBtn, &deleteBtn, &enableBtn, &soloBtn, &muteBtn, &autoBtn, &compBtn, &dynBtn, &scBtn,
             &shapeBox, &slopeBox, &typeBox, &filterBox, &filterSlopeBox, &noiseTypeBox, &clipBox, &fxBox, &detBox,
             &freq, &q, &drive, &bias, &fFreq, &fRes, &fGain, &wet, &gain, &pan, &fxRate, &fxDepth, &fxFeedback, &fxMix,
             &phat, &noise, &follow, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup, &ceiling,
             &dThresh, &dAttack, &dRelease, &detFreq, &detQ, &dGain, &dDrive, &dWet, &curve, &grMeter, &dynMeter, &chain })
        addAndMakeVisible (c);

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

    enableBtn.setOnColour (Colours::on.withAlpha (0.35f));
    soloBtn.setOnColour (Colours::warn.withAlpha (0.45f));
    muteBtn.setOnColour (Colours::collision.withAlpha (0.45f));
    autoBtn.setTooltip ("Auto gain: keeps the level roughly constant while you change the drive");
    muteBtn.setTooltip ("Mute: removes this band's frequency region from the output");
    dynBtn.setTooltip ("Dynamics: the detector level moves gain, drive and wet by their +/- ranges");
    scBtn.setTooltip ("Trigger the dynamics from the external side-chain input");
    detBox.setTooltip ("Detector: listen to the band's own region, or to an independent bell (drag the T handle on the display)");
    filterBox.setTooltip ("Band filter; its place in the signal chain is set in EFFECT CHAIN (drag the F handle on the display)");
    fxBox.setTooltip ("Modulation effect for this band");
    phat.slider.setTooltip ("Phat: sub-octave and saturated low end for weight");
    follow.slider.setTooltip ("Noise follows the band level (100 % = silent when the band is silent)");
    curve.setTooltip ("Transfer curve of the distortion (input -> output)");
    detFreq.slider.setTooltip ("Frequency of the detector bell");
    detQ.slider.setTooltip ("Width of the detector bell");
    prevBtn.onClick = [this] { stepBand (-1); };
    nextBtn.onClick = [this] { stepBand (1); };
    deleteBtn.onClick = [this]
    {
        if (band < 0) return;
        const int b = band;
        setBand (-1);
        proc.deleteBand (b);
        edited();
        if (onSelectBand) onSelectBand (-1);
    };

    setBand (-1);
    startTimerHz (20);
}

BandPanel::~BandPanel() { stopTimer(); }

void BandPanel::edited()
{
    juce::Component::SafePointer<BandPanel> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
}

void BandPanel::stepBand (int dir)
{
    if (band < 0) return;
    for (int i = 1; i <= kNumBands; ++i)
    {
        const int b = (band + dir * i + kNumBands * 2) % kNumBands;
        if (proc.refs.readBand (b).used) { if (onSelectBand) onSelectBand (b); return; }
    }
}

void BandPanel::setBand (int b)
{
    band = b;
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

    q.setEnabled (p.shape == Shape::Bell);
    slopeBox.setEnabled (p.shape != Shape::Full);
    freq.setEnabled (p.shape != Shape::Full);
    const bool crush = p.type == DistType::Bitcrush;
    bias.setCaption (crush ? "RATE" : "BIAS");
    drive.setCaption (crush ? "CRUSH" : "DRIVE");
    autoBtn.setEnabled (! crush);

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
    curve.set (p.type, p.driveDb, p.bias, p.autoGain, bandColour (band));
    chain.update (p);
}

void BandPanel::timerCallback()
{
    if (band < 0) return;
    refresh();
    if (band < 0) return;
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
        g.drawText ("Double-click the display to add a band, or click a band to edit it.",
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

    g.setFont (LookAndFeel::font (10.5f, true));
    for (int s = 0; s < numSections; ++s)
    {
        const auto r = sections[(size_t) s].toFloat();
        g.setColour (Colours::bg1.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        g.setColour (Colours::textDim);
        g.drawText (kSectionNames[s], r.withHeight (14.0f).translated (8.0f, 2.0f), juce::Justification::centredLeft);
    }
    g.setFont (LookAndFeel::font (11.0f));
    g.setColour (Colours::textDim);
    const auto cs = sections[secChain];
    g.drawText ("Signal flows left to right - drag a module to move it. The band's region goes in, wet / gain / pan come after.",
                cs.withTrimmedTop (60).reduced (10, 0).withHeight (16), juce::Justification::centredLeft);
}

void BandPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto head = r.removeFromTop (kHeader).withTrimmedRight (kTabsWidth);
    head.removeFromLeft (112);
    prevBtn.setBounds (head.removeFromLeft (28).reduced (0, 3));
    head.removeFromLeft (3);
    nextBtn.setBounds (head.removeFromLeft (28).reduced (0, 3));
    head.removeFromLeft (8);
    enableBtn.setBounds (head.removeFromLeft (44).reduced (0, 3));
    head.removeFromLeft (4);
    soloBtn.setBounds (head.removeFromLeft (52).reduced (0, 3));
    head.removeFromLeft (4);
    deleteBtn.setBounds (head.removeFromLeft (44).reduced (0, 3));

    r.removeFromTop (4);
    const int rowH = kKnobH + 18;
    auto row1 = r.removeFromTop (rowH);
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (rowH);
    r.removeFromTop (6);
    auto row3 = r.removeFromTop (rowH);

    auto place = [] (juce::Rectangle<int>& area, int width) { auto s = area.removeFromLeft (width); area.removeFromLeft (kGap); return s; };
    auto inner = [] (juce::Rectangle<int> s) { return s.withTrimmedTop (16).reduced (6, 2); };
    auto knobs = [] (juce::Rectangle<int>& area, std::initializer_list<Knob*> list)
    {
        for (auto* k : list) k->setBounds (area.removeFromLeft (kKnobW).withHeight (kKnobH));
    };
    auto combos = [] (juce::Rectangle<int>& area, std::initializer_list<juce::Component*> list)
    {
        auto col = area.removeFromLeft (kColW);
        area.removeFromLeft (4);
        const int h = 20, gap = 3;   // three items fit the 70 px control height
        int y = col.getY() + 4;
        for (auto* c : list) { c->setBounds (col.getX(), y, col.getWidth(), h); y += h + gap; }
    };

    // Row 1: region, distortion, filter, output
    {
        auto a = row1;
        sections[secRegion] = place (a, kColW + 4 + 2 * kKnobW + 12);
        sections[secDist]   = place (a, kColW + 4 + 2 * kKnobW + 92 + 12);
        sections[secFilter] = place (a, kColW + 4 + 3 * kKnobW + 12);
        sections[secOut]    = place (a, kColW + 4 + 3 * kKnobW + 12);

        auto s = inner (sections[secRegion]); combos (s, { &shapeBox, &slopeBox }); knobs (s, { &freq, &q });
        s = inner (sections[secDist]);   combos (s, { &typeBox, &autoBtn }); knobs (s, { &drive, &bias });
        curve.setBounds (s.removeFromLeft (88).withHeight (kKnobH - 2).reduced (2, 4));
        s = inner (sections[secFilter]); combos (s, { &filterBox, &filterSlopeBox }); knobs (s, { &fFreq, &fRes, &fGain });
        s = inner (sections[secOut]);    combos (s, { &muteBtn }); knobs (s, { &wet, &gain, &pan });
    }
    // Row 2: FX, phat / noise, compressor, clip
    {
        auto a = row2;
        sections[secFx]   = place (a, kColW + 4 + 4 * kKnobW + 12);
        sections[secPhat] = place (a, kKnobW + kColW + 4 + 2 * kKnobW + 12);
        sections[secComp] = place (a, kColW + 4 + 5 * kKnobW + 12);
        sections[secClip] = place (a, kColW + 4 + kKnobW + 12);

        auto s = inner (sections[secFx]);   combos (s, { &fxBox }); knobs (s, { &fxRate, &fxDepth, &fxFeedback, &fxMix });
        s = inner (sections[secPhat]);      knobs (s, { &phat }); combos (s, { &noiseTypeBox }); knobs (s, { &noise, &follow });
        s = inner (sections[secComp]);      combos (s, { &compBtn, &grMeter }); knobs (s, { &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup });
        s = inner (sections[secClip]);      combos (s, { &clipBox }); knobs (s, { &ceiling });
    }
    // Row 3: dynamics (with detector bell), effect chain
    {
        auto a = row3;
        sections[secDyn]   = place (a, kColW + 4 + 8 * kKnobW + 12);
        sections[secChain] = a;

        const auto ds = sections[secDyn];
        dynMeter.setBounds (ds.getX() + 80, ds.getY() + 3, 160, 11);
        auto s = inner (ds); combos (s, { &dynBtn, &scBtn, &detBox });
        knobs (s, { &dThresh, &dAttack, &dRelease, &detFreq, &detQ, &dGain, &dDrive, &dWet });
        chain.setBounds (inner (sections[secChain]).withTrimmedTop (4).withHeight (34).reduced (4, 0));
    }
}

} // namespace gkd::ui
