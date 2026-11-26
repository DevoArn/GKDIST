// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "ChainStrip.h"
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

    /** Header row height; the right part of it is left free for the page tabs. */
    static constexpr int kHeader = 26, kTabsWidth = 300;
    static constexpr int kHeight = 314;

    ChainStrip& getChainStrip() noexcept { return chain; }

private:
    void timerCallback() override;
    void refresh();
    void edited();
    void stepBand (int dir);

    GKDistProcessor& proc;
    int band = -1;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, deleteBtn { "DEL" };
    ToggleBox enableBtn { "ON" }, soloBtn { "SOLO" }, muteBtn { "MUTE" }, autoBtn { "AUTO" }, compBtn { "COMP" },
              dynBtn { "DYN" }, scBtn { "EXT SC" };
    ChoiceBox shapeBox { shapeNames() }, slopeBox { regionSlopeNames() }, typeBox { distTypeNames() },
              filterBox { bandFilterNames() }, filterSlopeBox { filterSlopeNames() }, noiseTypeBox { noiseTypeNames() },
              clipBox { clipModeNames() }, fxBox { fxTypeNames() }, detBox { detModeNames() };
    Knob freq { "FREQ" }, q { "Q" }, drive { "DRIVE" }, bias { "BIAS" },
         fFreq { "CUTOFF" }, fRes { "RESO" }, fGain { "GAIN" },
         wet { "WET" }, gain { "GAIN" }, pan { "PAN" },
         fxRate { "RATE" }, fxDepth { "DEPTH" }, fxFeedback { "FEEDBACK" }, fxMix { "MIX" },
         phat { "PHAT" }, noise { "NOISE" }, follow { "FOLLOW" },
         cThresh { "THRESH" }, cRatio { "RATIO" }, cAttack { "ATTACK" }, cRelease { "RELEASE" }, cMakeup { "MAKEUP" },
         ceiling { "CEILING" },
         dThresh { "THRESH" }, dAttack { "ATTACK" }, dRelease { "RELEASE" }, detFreq { "DET FREQ" }, detQ { "DET Q" },
         dGain { "+/- GAIN" }, dDrive { "+/- DRIVE" }, dWet { "+/- WET" };
    TransferCurve curve;
    Meter grMeter, dynMeter;
    ChainStrip chain;

    enum { secRegion, secDist, secFilter, secOut, secFx, secPhat, secComp, secClip, secDyn, secChain, numSections };
    std::array<juce::Rectangle<int>, numSections> sections;
};

} // namespace gkd::ui
