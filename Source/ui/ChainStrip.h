// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace gkd::ui
{

/** The band's effect chain as a row of tiles (signal flows left to right).
    Click a tile to edit that module, drag it to move the module in the
    chain; right-click for "Reset order". Each tile shows a short summary. */
class ChainStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChainStrip (GKDistProcessor& p);

    void setBand (int b);
    /** Refreshes order, summaries and which modules are active (call from a timer). */
    void update (const BandParams& p);
    void setSelectedModule (Module m);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onEdit;
    std::function<void (Module)> onModuleSelected;

    // For tests: the tile currently at a slot, and the centre of a slot.
    int moduleAt (int slot) const noexcept { return order[juce::jlimit (0, kNumModules - 1, slot)]; }
    juce::Point<float> slotCentre (int slot) const;

    static bool moduleActive (Module m, const BandParams& p);
    static juce::String moduleSummary (Module m, const BandParams& p);

private:
    juce::Rectangle<float> tileBounds (int slot) const;
    int slotAt (float x) const;
    void commitOrder();

    GKDistProcessor& proc;
    int band = -1;
    int order[kNumModules] { 0, 1, 2, 3, 4, 5, 6 };
    Module selectedModule = Module::Distortion;
    BandParams params;
    int dragSlot = -1;
    float dragX = 0.0f, grabOffset = 0.0f;
    bool moved = false;
};

} // namespace gkd::ui
