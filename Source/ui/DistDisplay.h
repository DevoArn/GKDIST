// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../dsp/FilterDesign.h"
#include "Spectrum.h"
#include "Theme.h"
#include <array>

namespace gkd::ui
{

/** Spectrum analyser with the bands drawn as regions; each band has a node
    (x = frequency, y = drive) that can be dragged. */
class DistDisplay : public juce::Component, private juce::Timer
{
public:
    explicit DistDisplay (GKDistProcessor& p);
    ~DistDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    int  getSelectedBand() const noexcept { return selected; }
    void selectBand (int b);

    std::function<void()> onSelectionChanged;
    std::function<void()> onEdit;

    // Mapping (public for tests)
    float  freqToX (double f) const noexcept;
    double xToFreq (float x) const noexcept;
    float  driveToY (double db) const noexcept;
    double yToDrive (float y) const noexcept;
    float  specToY (float db) const noexcept;
    juce::Point<float> nodePosition (int b) const;

    static constexpr double kMaxDriveDb = 48.0;

private:
    void timerCallback() override;
    void updateRegions();
    int  hitTest (juce::Point<float> p) const;
    void setBandValue (int b, int index, float v);
    void beginGesture (int b);
    void endGesture (int b);
    void showBandMenu (int b);
    void showBackgroundMenu (juce::Point<float> p);
    void createBandAt (juce::Point<float> p, Shape shape);
    void notifyEdit() { if (onEdit) onEdit(); }
    void paintGrid (juce::Graphics&);
    void paintSpectrum (juce::Graphics&, const float* lv, juce::Colour c, float alpha);

    GKDistProcessor& proc;
    juce::Rectangle<float> plot;
    SpectrumAnalyzer pre, post;
    double lastTime = 0.0;

    BandParams bands[kNumBands];
    struct Region
    {
        bool valid = false;
        double key[5] {};
        std::array<float, kSpecPoints> mag {};   // 0..1 region magnitude
    };
    std::array<Region, kNumBands> regions;
    double regionRate = 0.0;

    int selected = -1, hover = -1, dragBand = -1;
    juce::Point<float> dragStart, mousePos;
    bool mouseInside = false;
    double startFreq = 1000.0, startDrive = 12.0, startQ = 1.0;
    int wheelCommit = 0;
};

} // namespace gkd::ui
