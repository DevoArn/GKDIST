// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "FrameClock.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** Gain of the three OTT bands: boost to the right, cut to the left (+-24 dB). */
class OttMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setValues (float lowDb, float midDb, float highDb);
    void paint (juce::Graphics&) override;

private:
    float v[3] {};
};

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
    void frame (double dt);
    void edited();

    GKDistProcessor& proc;
    ChoiceBox osBox { oversamplingNames() }, slopeBox { globalSlopeNames() }, noiseTypeBox { noiseTypeNames() };
    ToggleBox ottBtn { "OTT" };
    Knob ottDepth { "DEPTH" }, ottTime { "TIME" }, ottUp { "UP" }, ottDown { "DOWN" }, ottIn { "IN" }, ottOut { "OUT" };
    OttMeter ottMeter;
    bool ottShown = false;
    void showOtt (bool on);
    ToggleBox hpBtn { "HP" }, lpBtn { "LP" }, compBtn { "COMP" }, clipBtn { "CLIP" }, softBtn { "SOFT" }, limitBtn { "LIMIT" }, bypassBtn { "BYPASS" };
    Knob hpFreq { "HP FREQ" }, lpFreq { "LP FREQ" }, cThresh { "THRESH" }, cRatio { "RATIO" }, cAttack { "ATTACK" },
         cRelease { "RELEASE" }, cMakeup { "MAKEUP" }, noise { "NOISE" }, clipCeil { "CEILING" },
         limCeil { "CEILING" }, limRel { "RELEASE" }, mix { "MIX" }, out { "OUTPUT" };
    Meter compMeter, limitMeter;

    enum { secOs, secFilter, secComp, secNoise, secClip, secLimit, secOut, numSections };
    std::array<juce::Rectangle<int>, numSections> sections;
    FrameClock clock { *this, [this] (double dt) { frame (dt); } };   // last: stops first
};

} // namespace gkd::ui
