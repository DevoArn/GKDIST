// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** All controls of the selected band. */
class BandPanel : public juce::Component, private juce::Timer
{
public:
    explicit BandPanel (GKDistProcessor& p);
    ~BandPanel() override;

    void setBand (int b);
    int  getBand() const noexcept { return band; }

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onEdit;
    std::function<void (int)> onSelectBand;

    static constexpr int kHeight = 214;

private:
    void timerCallback() override;
    void refresh();
    void edited();
    void stepBand (int dir);
    juce::Rectangle<int> section (int id) const { return sections[(size_t) id]; }

    GKDistProcessor& proc;
    int band = -1;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, deleteBtn { "DEL" };
    ToggleBox enableBtn { "ON" }, soloBtn { "SOLO" }, autoBtn { "AUTO" }, compBtn { "COMP" }, dynBtn { "DYN" }, scBtn { "EXT SC" };
    ChoiceBox shapeBox { shapeNames() }, slopeBox { regionSlopeNames() }, typeBox { distTypeNames() },
              filterBox { bandFilterNames() }, filterPosBox { filterPosNames() }, noiseTypeBox { noiseTypeNames() },
              clipBox { clipModeNames() };
    Knob freq { "FREQ" }, q { "Q" }, drive { "DRIVE" }, bias { "BIAS" }, fFreq { "CUTOFF" }, fRes { "RESO" },
         phat { "PHAT" }, noise { "NOISE" }, follow { "FOLLOW" }, wet { "WET" }, gain { "GAIN" },
         cThresh { "THRESH" }, cRatio { "RATIO" }, cAttack { "ATTACK" }, cRelease { "RELEASE" }, cMakeup { "MAKEUP" },
         ceiling { "CEILING" },
         dThresh { "THRESH" }, dAttack { "ATTACK" }, dRelease { "RELEASE" }, dGain { "+/- GAIN" }, dDrive { "+/- DRIVE" }, dWet { "+/- WET" };
    TransferCurve curve;
    Meter grMeter, dynMeter;

    enum { secRegion, secDist, secFilter, secPhat, secOut, secComp, secClip, secDyn, numSections };
    std::array<juce::Rectangle<int>, numSections> sections;
};

} // namespace gkd::ui
