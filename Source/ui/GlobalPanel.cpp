// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GlobalPanel.h"

namespace gkd::ui
{

namespace
{
    // Total width 1254 px: fits the 1280 px minimum window width.
    constexpr int kKnobW = 50, kKnobH = 68, kColW = 60, kGap = 6;
    const char* kNames[] = { "OVERSAMPLING", "FILTER", "COMPRESSOR", "NOISE", "CLIPPER", "LIMITER", "OUTPUT" };
}

GlobalPanel::GlobalPanel (GKDistProcessor& p) : proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> {
             &osBox, &slopeBox, &noiseTypeBox, &hpBtn, &lpBtn, &compBtn, &clipBtn, &softBtn, &limitBtn, &bypassBtn,
             &hpFreq, &lpFreq, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup, &noise, &clipCeil, &limCeil, &limRel,
             &mix, &out, &compMeter, &limitMeter })
        addAndMakeVisible (c);

    auto g = [this] (int idx) { return proc.param (globalParamId (idx)); };
    osBox.attach (g (gpOversampling)); slopeBox.attach (g (gpFiltSlope)); noiseTypeBox.attach (g (gpNoiseType));
    hpBtn.attach (g (gpHpOn)); lpBtn.attach (g (gpLpOn)); compBtn.attach (g (gpCompOn)); clipBtn.attach (g (gpClipOn));
    softBtn.attach (g (gpClipSoft)); limitBtn.attach (g (gpLimitOn)); bypassBtn.attach (g (gpBypass));
    hpFreq.attach (g (gpHpFreq)); lpFreq.attach (g (gpLpFreq)); cThresh.attach (g (gpCompThresh)); cRatio.attach (g (gpCompRatio));
    cAttack.attach (g (gpCompAttack)); cRelease.attach (g (gpCompRelease)); cMakeup.attach (g (gpCompMakeup));
    noise.attach (g (gpNoiseLevel)); clipCeil.attach (g (gpClipCeiling)); limCeil.attach (g (gpLimitCeiling));
    limRel.attach (g (gpLimitRelease)); mix.attach (g (gpMix)); out.attach (g (gpOutGain));

    for (auto* k : { &hpFreq, &lpFreq, &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup, &noise, &clipCeil, &limCeil, &limRel, &mix, &out })
        k->onEdit = [this] { edited(); };
    for (auto* t : { &hpBtn, &lpBtn, &compBtn, &clipBtn, &softBtn, &limitBtn, &bypassBtn })
        t->onEdit = [this] { edited(); };
    for (auto* c : { &osBox, &slopeBox, &noiseTypeBox })
        c->onEdit = [this] { edited(); };

    bypassBtn.setOnColour (Colours::warn.withAlpha (0.45f));
    osBox.setTooltip ("Oversampling: reduces aliasing from the distortion (adds latency)");
    limitBtn.setTooltip ("Brick-wall output limiter with 1 ms look-ahead: no sample exceeds the ceiling");
    softBtn.setTooltip ("Soft (tanh) or hard clipping");
    startTimerHz (15);
}

GlobalPanel::~GlobalPanel() { stopTimer(); }

void GlobalPanel::edited()
{
    juce::Component::SafePointer<GlobalPanel> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
}

void GlobalPanel::timerCallback()
{
    const auto gp = proc.refs.readGlobal();
    hpFreq.setEnabled (gp.hpOn);
    lpFreq.setEnabled (gp.lpOn);
    slopeBox.setEnabled (gp.hpOn || gp.lpOn);
    for (auto* k : { &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup }) k->setEnabled (gp.compOn);
    noiseTypeBox.setEnabled (gp.noiseDb > kNoiseOffDb + 0.05);
    clipCeil.setEnabled (gp.clipOn);
    softBtn.setEnabled (gp.clipOn);
    limCeil.setEnabled (gp.limitOn);
    limRel.setEnabled (gp.limitOn);
    const float cg = proc.getGlobalCompGr(), lg = gp.limitOn ? proc.getLimiterGr() : 0.0f;
    compMeter.setValue (cg / 24.0f, "GR " + juce::String (cg, 1) + " dB");
    limitMeter.setColour (Colours::collision);
    limitMeter.setValue (lg / 12.0f, "GR " + juce::String (lg, 1) + " dB");
}

void GlobalPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    g.setFont (LookAndFeel::font (10.5f, true));
    for (int s = 0; s < numSections; ++s)
    {
        const auto r = sections[(size_t) s].toFloat();
        g.setColour (Colours::bg1.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        g.setColour (Colours::textDim);
        g.drawText (kNames[s], r.withHeight (14.0f).translated (8.0f, 2.0f), juce::Justification::centredLeft);
    }
}

void GlobalPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto place = [&r] (int width) { auto s = r.removeFromLeft (width); r.removeFromLeft (kGap); return s; };
    auto inner = [] (juce::Rectangle<int> s) { return s.withTrimmedTop (16).reduced (6, 2); };
    auto knobs = [] (juce::Rectangle<int>& a, std::initializer_list<Knob*> list)
    {
        for (auto* k : list) k->setBounds (a.removeFromLeft (kKnobW).withHeight (kKnobH));
    };
    auto column = [] (juce::Rectangle<int>& a, int w, std::initializer_list<juce::Component*> list)
    {
        auto col = a.removeFromLeft (w);
        a.removeFromLeft (4);
        int y = col.getY() + 2;
        for (auto* c : list) { c->setBounds (col.getX(), y, col.getWidth(), 20); y += 23; }
    };

    sections[secOs]     = place (kColW + 14 + 12);
    sections[secFilter] = place (kColW + 4 + 2 * kKnobW + 12);
    sections[secComp]   = place (kColW + 4 + 5 * kKnobW + 12);
    sections[secNoise]  = place (kColW + 10 + 4 + kKnobW + 12);
    sections[secClip]   = place (kColW + 4 + kKnobW + 12);
    sections[secLimit]  = place (kColW + 4 + 2 * kKnobW + 12);
    sections[secOut]    = place (2 * kKnobW + 4 + kColW + 12);

    auto s = inner (sections[secOs]);     column (s, kColW + 14, { &osBox });
    s = inner (sections[secFilter]);      column (s, kColW, { &hpBtn, &lpBtn, &slopeBox }); knobs (s, { &hpFreq, &lpFreq });
    s = inner (sections[secComp]);        column (s, kColW, { &compBtn, &compMeter }); knobs (s, { &cThresh, &cRatio, &cAttack, &cRelease, &cMakeup });
    s = inner (sections[secNoise]);       column (s, kColW + 10, { &noiseTypeBox }); knobs (s, { &noise });
    s = inner (sections[secClip]);        column (s, kColW, { &clipBtn, &softBtn }); knobs (s, { &clipCeil });
    s = inner (sections[secLimit]);       column (s, kColW, { &limitBtn, &limitMeter }); knobs (s, { &limCeil, &limRel });
    s = inner (sections[secOut]);         knobs (s, { &mix, &out }); column (s, kColW, { &bypassBtn });
}

} // namespace gkd::ui
