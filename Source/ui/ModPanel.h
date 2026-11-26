// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** Live view of an LFO: its waveform and the current position. */
class LfoScope : public juce::Component
{
public:
    void set (LfoShape s, float value, float phase, juce::Colour c);
    void paint (juce::Graphics&) override;

private:
    LfoShape shape = LfoShape::Sine;
    float value = 0.0f, phase = 0.0f;
    juce::Colour colour = Colours::accent;
    std::array<float, 48> trail {};
    int trailPos = 0;
};

/** Centre-zero bar for a bipolar value (-1..1). */
class BipolarBar : public juce::Component
{
public:
    void setValue (float v, juce::Colour c);
    void paint (juce::Graphics&) override;

private:
    float value = 0.0f;
    juce::Colour colour = Colours::accent;
};

/** Modulation page: LFO 1 / 2, envelope followers and the 8-slot matrix
    (source -> band -> target, amount). */
class ModPanel : public juce::Component, private juce::Timer
{
public:
    explicit ModPanel (GKDistProcessor& p);
    ~ModPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onEdit;

private:
    void timerCallback() override;
    void edited();

    GKDistProcessor& proc;

    struct LfoUi
    {
        ChoiceBox shape { lfoShapeNames() }, div { lfoDivNames() };
        ToggleBox sync { "SYNC" };
        Knob rate { "RATE" };
        LfoScope scope;
    };
    LfoUi lfo[2];
    Knob envAttack { "ATTACK" }, envRelease { "RELEASE" };
    Meter inMeter, scMeter;

    struct SlotUi
    {
        ChoiceBox source { modSourceNames() }, band { modBandNames() }, target { modTargetNames() };
        juce::Slider amount;
        std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
        BipolarBar value;
    };
    std::array<SlotUi, kNumModSlots> slots;

    juce::Rectangle<int> lfoArea[2], envArea, matrixArea;
};

} // namespace gkd::ui
