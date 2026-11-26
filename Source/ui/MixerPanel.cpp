// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MixerPanel.h"
#include "BandPanel.h"

namespace gkd::ui
{

namespace
{
    constexpr float kMeterMin = -60.0f, kMeterMax = 6.0f;
    float meterNorm (float db) { return juce::jlimit (0.0f, 1.0f, (db - kMeterMin) / (kMeterMax - kMeterMin)); }
}

MixerStrip::MixerStrip (GKDistProcessor& p) : proc (p)
{
    for (auto* k : { &drive, &wet })
    {
        k->setCompact();
        k->onEdit = [this] { if (onEdit) onEdit(); };
        addAndMakeVisible (k);
    }
    pan.setSliderStyle (juce::Slider::LinearHorizontal);
    pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pan.onDragEnd = [this] { if (onEdit) onEdit(); };
    pan.onValueChange = [this] { repaint (panLabel); };
    pan.setTooltip ("Pan (double-click: centre)");
    addAndMakeVisible (pan);
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.onDragEnd = [this] { if (onEdit) onEdit(); };
    fader.onValueChange = [this] { repaint (gainLabel); };
    fader.setTooltip ("Band gain");
    addAndMakeVisible (fader);
    muteBtn.setOnColour (Colours::collision.withAlpha (0.5f));
    soloBtn.setOnColour (Colours::warn.withAlpha (0.5f));
    muteBtn.setTooltip ("Mute: removes this band's region");
    soloBtn.setTooltip ("Solo: listen to this band only");
    muteBtn.onEdit = [this] { if (onEdit) onEdit(); };
    soloBtn.onEdit = [this]
    {
        if (band >= 0 && soloBtn.getToggleState())
            for (int o = 0; o < kNumBands; ++o)
                if (o != band && proc.refs.readBand (o).solo) proc.setParam (bandParamId (o, bpSolo), 0.0f);
        if (onEdit) onEdit();
    };
    addAndMakeVisible (muteBtn);
    addAndMakeVisible (soloBtn);
}

void MixerStrip::setBand (int b)
{
    band = b;
    auto prm = [this] (int idx) { return band >= 0 ? proc.param (bandParamId (band, idx)) : nullptr; };
    drive.attach (prm (bpDrive));
    wet.attach (prm (bpWet));
    panAttachment.reset();
    if (auto* pp = prm (bpPan))
    {
        panAttachment = std::make_unique<juce::SliderParameterAttachment> (*pp, pan, nullptr);
        pan.setDoubleClickReturnValue (true, 0.0);
    }
    muteBtn.attach (prm (bpMute));
    soloBtn.attach (prm (bpSolo));
    faderAttachment.reset();
    if (auto* g = prm (bpGain))
    {
        faderAttachment = std::make_unique<juce::SliderParameterAttachment> (*g, fader, nullptr);
        fader.setDoubleClickReturnValue (true, 0.0);
    }
    if (band >= 0)
    {
        const auto col = bandColour (band);
        for (auto* k : { &drive, &wet }) k->setAccent (col);
        for (auto* s : { &fader, &pan })
        {
            s->setColour (juce::Slider::thumbColourId, col);
            s->setColour (juce::Slider::trackColourId, col.withAlpha (0.5f));
        }
    }
    meterDb = peakHold = -100.0f;
    tick (1.0);
}

void MixerStrip::tick (double dt)
{
    if (band < 0) return;
    const auto p = proc.refs.readBand (band);
    const auto t = proc.getTelemetry (band);
    const bool live = proc.isAudioRunning();
    const float lvl = live ? juce::jmax (-100.0f, t.levelDb) : -100.0f;
    const float oldMeter = meterDb, oldPeak = peakHold, oldInt = intensity;
    // instant attack, ~120 ms release; peak hold 1 s, then falls at 30 dB/s
    meterDb = lvl > meterDb ? lvl : smoothTowards (meterDb, lvl, dt, 0.12);
    if (lvl >= peakHold) { peakHold = lvl; holdTime = 1.0; }
    else if ((holdTime -= dt) < 0.0) peakHold = std::max (lvl, peakHold - (float) (30.0 * dt));
    const float drv = live ? t.driveDb : (float) p.driveDb, w = live ? t.wet : (float) p.wet;
    intensity = smoothTowards (intensity, juce::jlimit (0.0f, 1.0f, drv / 36.0f) * (0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, w)), dt, 0.03);
    const auto name = p.fxType != FxType::Off ? fxTypeNames()[(int) p.fxType] : distTypeNames()[(int) p.type];
    if (std::abs (meterDb - oldMeter) > 0.05f || std::abs (peakHold - oldPeak) > 0.05f)
        repaint (meterArea);
    if (std::abs (intensity - oldInt) > 0.002f || name != typeName)
    {
        typeName = name;
        repaint (0, 0, getWidth(), 34);
    }
}

void MixerStrip::resized()
{
    auto r = getLocalBounds().reduced (2, 0);
    r.removeFromTop (34);
    for (auto* k : { &drive, &wet }) k->setBounds (r.removeFromTop (60));
    panLabel = r.removeFromTop (12);
    pan.setBounds (r.removeFromTop (16));
    auto buttons = r.removeFromBottom (20);
    muteBtn.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
    soloBtn.setBounds (buttons.reduced (1, 0));
    r.removeFromBottom (2);
    gainLabel = r.removeFromBottom (12);
    r.removeFromTop (4);
    auto bottom = r;
    meterArea = bottom.removeFromRight (10).reduced (0, 2);
    fader.setBounds (bottom);
}

void MixerStrip::paint (juce::Graphics& g)
{
    if (band < 0) return;
    const auto p = proc.refs.readBand (band);
    const auto col = intensityColour (band, intensity);
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? Colours::bg1.brighter (0.08f) : Colours::bg1.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (selected ? juce::Colours::white.withAlpha (0.8f) : Colours::panelEdge);
    g.drawRoundedRectangle (r, 4.0f, selected ? 1.4f : 1.0f);

    // header: colour (saturation follows intensity), number, type
    auto head = r.removeFromTop (32.0f).reduced (3.0f, 3.0f);
    g.setColour (col.withAlpha (p.enabled ? 0.85f : 0.3f));
    g.fillRoundedRectangle (head.removeFromTop (14.0f), 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (LookAndFeel::font (11.0f, true));
    g.drawText (juce::String (band + 1), head.withY (head.getY() - 14.0f).withHeight (14.0f), juce::Justification::centred);
    g.setColour (Colours::textDim);
    g.setFont (LookAndFeel::font (9.5f));
    g.drawFittedText (typeName, head.toNearestInt(), juce::Justification::centred, 1, 0.8f);

    // level meter
    const auto m = meterArea.toFloat();
    g.setColour (Colours::bg0);
    g.fillRoundedRectangle (m, 2.0f);
    const float v = meterNorm (meterDb);
    if (v > 0.0f)
    {
        const auto bar = m.withTop (m.getBottom() - v * m.getHeight());
        juce::ColourGradient grad (col, 0.0f, m.getBottom(), meterDb > 0.0f ? Colours::collision : col.brighter (0.3f), 0.0f, m.getY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, 2.0f);
    }
    if (peakHold > kMeterMin)
    {
        const float py = m.getBottom() - meterNorm (peakHold) * m.getHeight();
        g.setColour (peakHold > 0.0f ? Colours::collision : juce::Colours::white.withAlpha (0.8f));
        g.fillRect (m.getX(), py - 1.0f, m.getWidth(), 2.0f);
    }
    const float zy = m.getBottom() - meterNorm (0.0f) * m.getHeight();
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawHorizontalLine ((int) zy, m.getX() - 2.0f, m.getRight());

    // pan and gain values
    g.setFont (LookAndFeel::font (9.5f, true));
    g.setColour (Colours::textDim);
    const int pc = (int) std::lround (pan.getValue());
    g.drawText ("PAN " + (pc == 0 ? juce::String ("C") : (pc < 0 ? "L" + juce::String (-pc) : "R" + juce::String (pc))),
                panLabel, juce::Justification::centred);
    g.setColour (Colours::text);
    g.drawText (formatDb (fader.getValue()), gainLabel, juce::Justification::centred);
}

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < 34 && onSelect) onSelect (band);
}

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < 34 && onOpen) onOpen (band);
}

// ---------------------------------------------------------------------------
MixerPanel::MixerPanel (GKDistProcessor& p) : proc (p)
{
    for (auto& s : strips)
    {
        s = std::make_unique<MixerStrip> (proc);
        s->onSelect = [this] (int b) { if (onSelectBand) onSelectBand (b); };
        s->onOpen = [this] (int b) { if (onOpenBand) onOpenBand (b); };
        s->onEdit = [this]
        {
            juce::Component::SafePointer<MixerPanel> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
        };
        addChildComponent (*s);
    }
    usedNow.fill (false);
    rebuild();
    startTimerHz (15);   // structure only; the meters run on the frame clock
}

MixerPanel::~MixerPanel() { stopTimer(); }

void MixerPanel::setSelectedBand (int b)
{
    selectedBand = b;
    for (auto& s : strips) s->setSelected (s->isVisible() && s->getBand() == b);
}

void MixerPanel::rebuild()
{
    numVisible = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        usedNow[(size_t) b] = proc.refs.readBand (b).used;
        if (usedNow[(size_t) b])
        {
            auto& s = strips[(size_t) numVisible++];
            if (s->getBand() != b) s->setBand (b);
            s->setVisible (true);
        }
    }
    for (int i = numVisible; i < kNumBands; ++i)
    {
        if (strips[(size_t) i]->getBand() != -1) strips[(size_t) i]->setBand (-1);
        strips[(size_t) i]->setVisible (false);
    }
    setSelectedBand (selectedBand);
    resized();
    repaint();
}

void MixerPanel::timerCallback()
{
    bool changed = false;
    for (int b = 0; b < kNumBands; ++b)
        if (proc.refs.readBand (b).used != usedNow[(size_t) b]) { changed = true; break; }
    if (changed) rebuild();
}

void MixerPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (BandPanel::kHeader + 4);
    r.removeFromBottom (6);
    for (int i = 0; i < numVisible; ++i)
        strips[(size_t) i]->setBounds (r.removeFromLeft (MixerStrip::kWidth).withTrimmedRight (1));
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);
    g.setFont (LookAndFeel::font (15.0f, true));
    g.setColour (Colours::text);
    g.drawText ("Mixer", 12, 3, 80, 20, juce::Justification::centredLeft);
    g.setFont (LookAndFeel::font (11.0f));
    g.setColour (Colours::textDim);
    g.drawText ("Click a strip header to select the band, double-click to edit it. Colours get stronger the harder a band is driven.",
                90, 3, getWidth() - 90 - BandPanel::kTabsWidth, 20, juce::Justification::centredLeft);
    if (numVisible == 0)
    {
        g.setFont (LookAndFeel::font (15.0f));
        g.drawText ("No bands yet: double-click the display to add one.", getLocalBounds().withTrimmedTop (BandPanel::kHeader),
                    juce::Justification::centred);
    }
}

} // namespace gkd::ui
