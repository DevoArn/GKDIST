// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ModPanel.h"
#include "BandPanel.h"

namespace gkd::ui
{

namespace
{
    juce::Colour lfoColour (int l) { return l == 0 ? Colours::accent : Colours::accent2; }
    constexpr int kRowH = 28;

    /** Deterministic shape for drawing (random shapes are drawn as a fixed example). */
    float drawShape (LfoShape s, float ph)
    {
        constexpr float twoPi = 6.2831853f;
        static const float steps[] = { 0.6f, -0.3f, 0.9f, -0.8f, 0.2f, -0.5f, 0.7f, -0.1f };
        switch (s)
        {
            case LfoShape::Sine:       return std::sin (twoPi * ph);
            case LfoShape::Triangle:   return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case LfoShape::SawUp:      return 2.0f * ph - 1.0f;
            case LfoShape::SawDown:    return 1.0f - 2.0f * ph;
            case LfoShape::Square:     return ph < 0.5f ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return steps[juce::jlimit (0, 7, (int) (ph * 8.0f))];
            case LfoShape::SmoothRandom:
            {
                const float x = ph * 8.0f;
                const int i = juce::jlimit (0, 7, (int) x);
                const float t = 0.5f - 0.5f * std::cos (3.1415927f * (x - (float) i));
                return steps[i] + t * (steps[(i + 1) % 8] - steps[i]);
            }
            case LfoShape::Count:
            default:                   return 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
void LfoScope::set (LfoShape s, float v, float ph, juce::Colour c)
{
    shape = s; value = v; phase = ph; colour = c;
    trail[(size_t) trailPos] = v;
    trailPos = (trailPos + 1) % (int) trail.size();
    repaint();
}

void LfoScope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::bg0);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::grid);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    const auto in = r.reduced (4.0f, 6.0f);
    juce::Path p;
    for (int i = 0; i <= 64; ++i)
    {
        const float ph = (float) i / 64.0f;
        const float x = in.getX() + ph * in.getWidth(), y = in.getCentreY() - drawShape (shape, ph) * 0.5f * in.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour.withAlpha (0.55f));
    g.strokePath (p, juce::PathStrokeType (1.4f));
    // current value: dot at the phase, plus a live bar on the right edge
    const float x = in.getX() + juce::jlimit (0.0f, 1.0f, phase) * in.getWidth();
    const float y = in.getCentreY() - juce::jlimit (-1.0f, 1.0f, value) * 0.5f * in.getHeight();
    g.setColour (colour);
    g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    g.setColour (colour.withAlpha (0.35f));
    g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
}

void BipolarBar::setValue (float v, juce::Colour c)
{
    v = juce::jlimit (-1.0f, 1.0f, v);
    if (std::abs (v - value) < 0.002f && c == colour) return;
    value = v;
    colour = c;
    repaint();
}

void BipolarBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 5.0f);
    g.setColour (Colours::bg0);
    g.fillRoundedRectangle (r, 3.0f);
    const float cx = r.getCentreX();
    const float w = value * 0.5f * r.getWidth();
    g.setColour (colour);
    if (w >= 0.0f) g.fillRect (cx, r.getY(), w, r.getHeight());
    else           g.fillRect (cx + w, r.getY(), -w, r.getHeight());
    g.setColour (Colours::textDim);
    g.drawVerticalLine ((int) cx, r.getY() - 2.0f, r.getBottom() + 2.0f);
}

// ---------------------------------------------------------------------------
ModPanel::ModPanel (GKDistProcessor& p) : proc (p)
{
    const int rateIdx[2] { gpLfo1Rate, gpLfo2Rate }, shapeIdx[2] { gpLfo1Shape, gpLfo2Shape };
    const int syncIdx[2] { gpLfo1Sync, gpLfo2Sync }, divIdx[2] { gpLfo1Div, gpLfo2Div };
    for (int l = 0; l < 2; ++l)
    {
        auto& u = lfo[l];
        for (auto* c : std::initializer_list<juce::Component*> { &u.shape, &u.div, &u.sync, &u.rate, &u.scope })
            addAndMakeVisible (c);
        u.shape.attach (proc.param (globalParamId (shapeIdx[l])));
        u.div.attach (proc.param (globalParamId (divIdx[l])));
        u.sync.attach (proc.param (globalParamId (syncIdx[l])));
        u.rate.attach (proc.param (globalParamId (rateIdx[l])));
        u.rate.setAccent (lfoColour (l));
        u.sync.setOnColour (lfoColour (l).withAlpha (0.45f));
        u.sync.setTooltip ("Sync the LFO to the host tempo (follows the song position while playing)");
        u.shape.onEdit = [this] { edited(); };
        u.div.onEdit = [this] { edited(); };
        u.sync.onEdit = [this] { edited(); };
        u.rate.onEdit = [this] { edited(); };
    }
    for (auto* c : std::initializer_list<juce::Component*> { &envAttack, &envRelease, &inMeter, &scMeter })
        addAndMakeVisible (c);
    envAttack.attach (proc.param (globalParamId (gpEnvAttack)));
    envRelease.attach (proc.param (globalParamId (gpEnvRelease)));
    envAttack.onEdit = [this] { edited(); };
    envRelease.onEdit = [this] { edited(); };
    inMeter.setColour (Colours::on);
    scMeter.setColour (Colours::scSpec);

    for (int s = 0; s < kNumModSlots; ++s)
    {
        auto& u = slots[(size_t) s];
        for (auto* c : std::initializer_list<juce::Component*> { &u.source, &u.band, &u.target, &u.amount, &u.value })
            addAndMakeVisible (c);
        u.source.attach (proc.param (modParamId (s, mpSource)));
        u.band.attach (proc.param (modParamId (s, mpBand)));
        u.target.attach (proc.param (modParamId (s, mpTarget)));
        u.amount.setSliderStyle (juce::Slider::LinearHorizontal);
        u.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 18);
        u.amount.setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.6f));
        u.amountAttachment = std::make_unique<juce::SliderParameterAttachment> (*proc.param (modParamId (s, mpAmount)), u.amount, nullptr);
        u.amount.setDoubleClickReturnValue (true, 0.0);
        u.amount.onDragEnd = [this] { edited(); };
        u.source.onEdit = [this] { edited(); };
        u.band.onEdit = [this] { edited(); };
        u.target.onEdit = [this] { edited(); };
        u.source.setTooltip ("Modulation source");
        u.band.setTooltip ("Band to modulate (or all bands)");
        u.target.setTooltip ("Band parameter to modulate");
        u.amount.setTooltip ("Amount (negative inverts); 100 % = the full range of the target");
    }
    startTimerHz (30);
}

ModPanel::~ModPanel() { stopTimer(); }

void ModPanel::edited()
{
    juce::Component::SafePointer<ModPanel> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
}

void ModPanel::timerCallback()
{
    const auto g = proc.refs.readGlobal();
    const bool live = proc.isAudioRunning();
    for (int l = 0; l < 2; ++l)
    {
        auto& u = lfo[l];
        u.rate.setEnabled (! g.lfoSync[l]);
        u.div.setEnabled (g.lfoSync[l]);
        if (isVisible())   // only the active page animates
            u.scope.set (g.lfoShape[l], live ? proc.getLfoValue (l) : 0.0f, live ? proc.getLfoPhase (l) : 0.0f, lfoColour (l));
    }
    if (! isVisible()) return;
    const float ein = live ? proc.getEnvValue (0) : 0.0f, esc = live ? proc.getEnvValue (1) : 0.0f;
    inMeter.setValue (ein, "Input " + formatNumber (ein * 100.0f, 0) + " %");
    scMeter.setValue (esc, proc.isSidechainActive() ? "Side-chain " + formatNumber (esc * 100.0f, 0) + " %" : juce::String ("Side-chain (none)"));
    for (int s = 0; s < kNumModSlots; ++s)
    {
        auto& u = slots[(size_t) s];
        const auto slot = proc.refs.readModSlot (s);
        const bool on = slot.source != ModSource::Off;
        u.band.setEnabled (on);
        u.target.setEnabled (on);
        u.amount.setEnabled (on);
        const juce::Colour c = slot.source == ModSource::Lfo1 ? lfoColour (0)
                             : slot.source == ModSource::Lfo2 ? lfoColour (1)
                             : slot.source == ModSource::InputEnv ? Colours::on : Colours::scSpec;
        u.value.setValue (on && live ? proc.getModSlotValue (s) : 0.0f, c);
    }
}

void ModPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (BandPanel::kHeader + 4);
    r.removeFromBottom (6);

    auto left = r.removeFromLeft (300);
    r.removeFromLeft (10);
    const int boxH = (left.getHeight() - 6) / 2;
    for (int l = 0; l < 2; ++l)
    {
        lfoArea[l] = left.removeFromTop (boxH);
        left.removeFromTop (6);
        auto in = lfoArea[l].withTrimmedTop (18).reduced (6, 4);
        auto col = in.removeFromLeft (96);
        auto& u = lfo[l];
        u.shape.setBounds (col.removeFromTop (20));
        col.removeFromTop (3);
        u.sync.setBounds (col.removeFromTop (20));
        col.removeFromTop (3);
        u.div.setBounds (col.removeFromTop (20));
        in.removeFromLeft (4);
        u.rate.setBounds (in.removeFromLeft (56).withHeight (72));
        in.removeFromLeft (4);
        u.scope.setBounds (in.withHeight (std::min (in.getHeight(), 92)));
    }

    envArea = r.removeFromLeft (200).withHeight (boxH);
    r.removeFromLeft (10);
    {
        auto in = envArea.withTrimmedTop (18).reduced (6, 4);
        auto knobs = in.removeFromTop (72);
        envAttack.setBounds (knobs.removeFromLeft (56));
        knobs.removeFromLeft (4);
        envRelease.setBounds (knobs.removeFromLeft (56));
        in.removeFromTop (2);
        inMeter.setBounds (in.removeFromTop (16));
        in.removeFromTop (3);
        scMeter.setBounds (in.removeFromTop (16));
    }

    matrixArea = r;
    auto rows = r.withTrimmedTop (34).reduced (6, 0);
    for (int s = 0; s < kNumModSlots; ++s)
    {
        auto row = rows.removeFromTop (kRowH).reduced (0, 3);
        row.removeFromLeft (22);
        auto& u = slots[(size_t) s];
        u.source.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (6);
        u.band.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (6);
        u.target.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (6);
        u.value.setBounds (row.removeFromRight (80));
        row.removeFromRight (6);
        u.amount.setBounds (row);
    }
}

void ModPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);
    g.setFont (LookAndFeel::font (15.0f, true));
    g.setColour (Colours::text);
    g.drawText ("Modulation", 12, 3, 110, 20, juce::Justification::centredLeft);
    g.setFont (LookAndFeel::font (11.0f));
    g.setColour (Colours::textDim);
    g.drawText ("Route LFOs and envelopes to any band parameter. Several slots can target the same parameter; their amounts add up.",
                124, 3, getWidth() - 124 - BandPanel::kTabsWidth, 20, juce::Justification::centredLeft);

    auto section = [&g] (juce::Rectangle<int> r, const juce::String& name)
    {
        g.setColour (Colours::bg1.withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r.toFloat(), 5.0f, 1.0f);
        g.setColour (Colours::textDim);
        g.setFont (LookAndFeel::font (10.5f, true));
        g.drawText (name, r.withHeight (14).translated (8, 2), juce::Justification::centredLeft);
    };
    section (lfoArea[0], "LFO 1");
    section (lfoArea[1], "LFO 2");
    section (envArea, "ENVELOPE FOLLOWERS");
    section (matrixArea, "MATRIX");

    // column labels
    g.setFont (LookAndFeel::font (10.0f, true));
    g.setColour (Colours::textDim);
    auto lab = matrixArea.withTrimmedTop (16).withHeight (16).reduced (6, 0);
    lab.removeFromLeft (22);
    g.drawText ("SOURCE", lab.removeFromLeft (146), juce::Justification::centredLeft);
    g.drawText ("BAND", lab.removeFromLeft (106), juce::Justification::centredLeft);
    g.drawText ("TARGET", lab.removeFromLeft (126), juce::Justification::centredLeft);
    g.drawText ("NOW", lab.removeFromRight (80), juce::Justification::centred);
    g.drawText ("AMOUNT", lab, juce::Justification::centredLeft);
    auto rows = matrixArea.withTrimmedTop (34).reduced (6, 0);
    g.setFont (LookAndFeel::font (11.0f, true));
    for (int s = 0; s < kNumModSlots; ++s)
        g.drawText (juce::String (s + 1), rows.removeFromTop (kRowH).removeFromLeft (18), juce::Justification::centredLeft);
}

} // namespace gkd::ui
