// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Controls.h"
#include "ChainStrip.h"
#include "FrameClock.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** A chip for each of the 24 bands: colour (intensity), selection, click to select. */
class BandChips : public juce::Component, public juce::TooltipClient
{
public:
    explicit BandChips (GKDistProcessor& p) : proc (p) {}
    void setSelected (int b) { if (b != selected) { selected = b; repaint(); } }
    void tick (double dt = 1.0);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = chipAt (e.position.x); }
    juce::String getTooltip() override;

    std::function<void (int)> onSelect;

private:
    int chipAt (float x) const;
    GKDistProcessor& proc;
    int selected = -1, hover = -1;
    BandParams bands[kNumBands];
    float intensity[kNumBands] {};
};

/** The selected band: region and output on the left, the effect chain with an
    editor page for the module picked in the chain in the middle, dynamics on
    the right. */
class BandPanel : public juce::Component, private juce::Timer
{
public:
    explicit BandPanel (GKDistProcessor& p);
    ~BandPanel() override;

    void setBand (int b);
    int  getBand() const noexcept { return band; }
    /** The module whose controls are shown (click a tile in the chain to change it). */
    void showModule (Module m);
    Module getModule() const noexcept { return module; }

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
    void frame (double dt);   // every display frame: meters, chips, live curves
    void refresh();
    void edited();
    void layoutModulePage();

    GKDistProcessor& proc;
    int band = -1;
    Module module = Module::Distortion;

    juce::TextButton deleteBtn { "DEL" };
    ToggleBox enableBtn { "ON" }, soloBtn { "SOLO" }, muteBtn { "MUTE" }, autoBtn { "AUTO GAIN" }, compBtn { "COMP ON" },
              dynBtn { "DYN" }, scBtn { "EXT SC" };
    ChoiceBox shapeBox { shapeNames() }, slopeBox { regionSlopeNames() }, typeBox { distTypeNames() },
              filterBox { bandFilterNames() }, filterSlopeBox { filterSlopeNames() }, noiseTypeBox { noiseTypeNames() },
              clipBox { clipModeNames() }, fxBox { fxTypeNames() }, detBox { detModeNames() };
    Knob freq { "FREQ" }, q { "Q" }, drive { "DRIVE" }, bias { "BIAS" },
         fFreq { "CUTOFF" }, fRes { "RESO" }, fGain { "GAIN" },
         wet { "WET" }, gain { "GAIN" }, pan { "PAN" },
         fxRate { "RATE" }, fxDepth { "DEPTH" }, fxFeedback { "FEEDBACK" }, fxMix { "MIX" },
         phat { "PHAT" }, noise { "LEVEL" }, follow { "FOLLOW" },
         cThresh { "THRESH" }, cRatio { "RATIO" }, cAttack { "ATTACK" }, cRelease { "RELEASE" }, cMakeup { "MAKEUP" },
         ceiling { "CEILING" },
         dThresh { "THRESH" }, dAttack { "ATTACK" }, dRelease { "RELEASE" }, detFreq { "DET FREQ" }, detQ { "DET Q" },
         dGain { "+/- GAIN" }, dDrive { "+/- DRIVE" }, dWet { "+/- WET" };
    TransferCurve curve;
    ResponseCurve response;
    Meter grMeter, dynMeter;
    ChainStrip chain;
    BandChips chips;

    std::array<std::vector<juce::Component*>, kNumModules> pageControls;
    juce::Rectangle<int> secRegion, secOut, secChain, secDyn, pageArea;

    // live values of the selected band (curves move with dynamics and modulation)
    float liveDrive = 0, liveBias = 0, liveFiltLogF = 0, liveFiltLogR = 0, liveFiltGain = 0;
    int liveBand = -1;
    FrameClock clock { *this, [this] (double dt) { frame (dt); } };   // last: stops first
};

} // namespace gkd::ui
