// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"
#include <vector>

namespace gkd::ui
{

/** Scrolling waveform: the input in grey behind the output, which is coloured
    by the bands that change the signal at each moment (colours mixed by how
    much each band changes it). Right-click: time span, zoom, freeze;
    double-click: freeze; wheel: zoom. */
class WaveformView : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView (GKDistProcessor& p);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kHeight = 140;

    // For tests
    int  columnsStored() const noexcept { return hist.count; }
    /** Band that changes the signal most around x (-1: none), and its share 0..1. */
    int  dominantBand (float x, float* share = nullptr) const;
    void pullNow() { timerCallback(); }

private:
    struct Pixel
    {
        float inMin = 0, inMax = 0, outMin = 0, outMax = 0, total = 0;
        float effect[kNumBands] {};
        int n = 0;
    };

    void timerCallback() override;
    void resizeHistory();
    juce::Rectangle<float> plotArea() const;
    int  visibleColumns() const;
    Pixel aggregate (int x, int width) const;
    void showMenu();

    GKDistProcessor& proc;
    GKDistProcessor::WaveHistory& hist;
    std::vector<dsp::WaveColumn> scratch;
    bool frozen = false;
    float mouseX = -1.0f;
    static constexpr double kMaxSeconds = 10.0;
};

} // namespace gkd::ui
