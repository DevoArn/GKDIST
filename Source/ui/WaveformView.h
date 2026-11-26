// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Theme.h"
#include "FrameClock.h"
#include "../PluginProcessor.h"
#include <vector>

namespace gkd::ui
{

/** Scrolling waveform: the input in grey behind the output, which is coloured
    by the bands that change the signal at each moment (colours mixed by how
    much each band changes it). It scrolls smoothly at the display's refresh
    rate. SPEED (or Ctrl+wheel) sets the visible time, 0.25 - 20 s.
    Wheel: zoom; double-click: freeze; right-click: menu. */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (GKDistProcessor& p);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int kHeight = 150;
    static constexpr double kMinSeconds = 0.25, kMaxSeconds = 20.0;

    /** Visible time window in seconds (smaller = faster scrolling). */
    double getSeconds() const;
    void   setSeconds (double s);

    // For tests
    int  columnsStored() const noexcept { return hist.count; }
    /** Band that changes the signal most around x (-1: none), and its share 0..1. */
    int  dominantBand (float x, float* share = nullptr) const;
    void pullNow() { frame (1.0 / 60.0); }
    double getHead() const noexcept { return head; }

private:
    struct Pixel
    {
        float inMin = 0, inMax = 0, outMin = 0, outMax = 0, total = 0;
        float effect[kNumBands] {};
        int n = 0;
    };

    void frame (double dt);
    void pull();
    void resizeHistory();
    juce::Rectangle<float> plotArea() const;
    double columnsPerSecond() const noexcept { return hist.rate / dsp::DistEngine::kWaveColumnSamples; }
    Pixel aggregate (int x, int width) const;
    void showMenu();
    void updateSpeedText();

    GKDistProcessor& proc;
    GKDistProcessor::WaveHistory& hist;
    std::vector<dsp::WaveColumn> scratch;
    juce::Slider speed;
    juce::Rectangle<int> speedLabel, speedValue;
    double head = 0.0;        // displayed position (in columns) of the right edge; moves smoothly
    double burst = 16.0;      // average columns per arrival (host block size / 32)
    bool frozen = false;
    float mouseX = -1.0f;
    PaintBudget budget { 0.25 };
    FrameClock clock { *this, [this] (double dt) { frame (dt); } };   // last: stops first
};

} // namespace gkd::ui
