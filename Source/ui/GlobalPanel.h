// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** Global output section: oversampling, filters, compressor, noise, clipper,
    limiter, mix and output. */
class GlobalPanel : public juce::Component, private juce::Timer
{
public:
    explicit GlobalPanel (GKDistProcessor& p);
    ~GlobalPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onEdit;
    static constexpr int kHeight = 104;

private:
    void timerCallback() override;
    void edited();

    GKDistProcessor& proc;
    ChoiceBox osBox { oversamplingNames() }, slopeBox { globalSlopeNames() }, noiseTypeBox { noiseTypeNames() };
    ToggleBox hpBtn { "HP" }, lpBtn { "LP" }, compBtn { "COMP" }, clipBtn { "CLIP" }, softBtn { "SOFT" }, limitBtn { "LIMIT" }, bypassBtn { "BYPASS" };
    Knob hpFreq { "HP FREQ" }, lpFreq { "LP FREQ" }, cThresh { "THRESH" }, cRatio { "RATIO" }, cAttack { "ATTACK" },
         cRelease { "RELEASE" }, cMakeup { "MAKEUP" }, noise { "NOISE" }, clipCeil { "CEILING" },
         limCeil { "CEILING" }, limRel { "RELEASE" }, mix { "MIX" }, out { "OUTPUT" };
    Meter compMeter, limitMeter;

    enum { secOs, secFilter, secComp, secNoise, secClip, secLimit, secOut, numSections };
    std::array<juce::Rectangle<int>, numSections> sections;
};

} // namespace gkd::ui
