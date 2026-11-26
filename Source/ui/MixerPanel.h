// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "FrameClock.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** One mixer channel strip: drive, wet, pan, gain fader with level meter, mute, solo. */
class MixerStrip : public juce::Component
{
public:
    explicit MixerStrip (GKDistProcessor& p);

    void setBand (int b);
    int  getBand() const noexcept { return band; }
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void tick (double dt);   // meter + header refresh (every display frame)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void (int)> onSelect, onOpen;
    std::function<void()> onEdit;

    static constexpr int kWidth = 51;

private:
    GKDistProcessor& proc;
    int band = -1;
    bool selected = false;
    float meterDb = -100.0f, peakHold = -100.0f, intensity = 0.0f;
    double holdTime = 0.0;
    juce::String typeName;

    Knob drive { "DRIVE" }, wet { "WET" };
    juce::Slider pan, fader;
    std::unique_ptr<juce::SliderParameterAttachment> panAttachment, faderAttachment;
    juce::Rectangle<int> panLabel, gainLabel;
    ToggleBox muteBtn { "M" }, soloBtn { "S" };
    juce::Rectangle<int> meterArea;
};

/** Mixer page: a strip for every band in use. */
class MixerPanel : public juce::Component, private juce::Timer
{
public:
    explicit MixerPanel (GKDistProcessor& p);
    ~MixerPanel() override;

    void setSelectedBand (int b);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (int)> onSelectBand, onOpenBand;
    std::function<void()> onEdit;

    /** For tests: number of visible strips. */
    int numVisibleStrips() const noexcept { return numVisible; }

private:
    void timerCallback() override;
    void rebuild();

    GKDistProcessor& proc;
    std::array<std::unique_ptr<MixerStrip>, kNumBands> strips;
    std::array<bool, kNumBands> usedNow {};
    int numVisible = 0, selectedBand = -1;
    FrameClock clock { *this, [this] (double dt)
    {
        if (! isVisible()) return;   // only the active page animates
        for (int i = 0; i < numVisible; ++i) strips[(size_t) i]->tick (dt);
    } };   // last: stops first
};

} // namespace gkd::ui
