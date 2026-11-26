// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../dsp/FilterDesign.h"
#include "Spectrum.h"
#include "Theme.h"
#include "FrameClock.h"
#include <array>

namespace gkd::ui
{

/** Spectrum analyser with the bands drawn as regions; each band has a node
    (x = frequency, y = drive) that can be dragged.

    Visual feedback: a band's colour gets more saturated and brighter the
    harder it is driven, it glows with its live output level, overlapping
    bands mix their colours, and red marks show where two active bands
    compete for the same frequencies (clashes). The selected band also shows
    its filter curve (handle "F") and, with dynamics on, its detector bell
    with the threshold (handle "T") and the live detector level. */
class DistDisplay : public juce::Component
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
    double yToSpec (float y) const noexcept;
    float  filterDbToY (double db) const noexcept;
    double yToFilterDb (float y) const noexcept;
    juce::Point<float> nodePosition (int b) const;
    juce::Point<float> filterHandlePosition (int b) const;
    juce::Point<float> detectorHandlePosition (int b) const;
    /** Tab of an OTT crossover (0 = low, 1 = high) when the OTT compressor is on, else empty. */
    juce::Rectangle<float> crossoverTab (int which) const;
    /** Strongest clash (0..1) between two bands at the moment. */
    float clashBetween (int a, int b) const noexcept { return clash[(size_t) a][(size_t) b]; }
    /** Visual intensity of a band (0..1): drive x wet. */
    float bandIntensity (int b) const noexcept { return intensity[(size_t) b]; }

    static constexpr double kMaxDriveDb = 48.0;

    /** Diagnostics: accumulated paint time per stage (ms) while profiling is on. */
    struct PaintProfile { bool on = false; double background = 0, spectra = 0, bands = 0, extras = 0, nodes = 0, readout = 0,
                                                  mixFill = 0, clash = 0, outlines = 0; };
    PaintProfile paintProfile;
    static constexpr double kFilterRangeDb = 30.0;   // filter curve: +-30 dB over half the height

private:
    enum class Handle { None, Node, Filter, Detector, Crossover };   // Crossover: band 0 = low, 1 = high
    struct Hit { Handle what = Handle::None; int band = -1; };

    void frame (double dt);
    void updateRegions();
    void updateSelectedCurves();
    void updateMixAndClashes();
    Hit  hitTest (juce::Point<float> p) const;
    void setBandValue (int b, int index, float v);
    void beginGesture (int b, std::initializer_list<int> params);
    void endGesture();
    void showBandMenu (int b);
    void showBackgroundMenu (juce::Point<float> p);
    void createBandAt (juce::Point<float> p, Shape shape);
    void notifyEdit() { if (onEdit) onEdit(); }
    void paintGrid (juce::Graphics&);
    void paintSpectrum (juce::Graphics&, const float* lv, juce::Colour c, float alpha);
    void paintBands (juce::Graphics&, int solo);
    void paintSelectedExtras (juce::Graphics&);
    void paintNodes (juce::Graphics&, int solo);
    void paintReadout (juce::Graphics&);
    void paintCrossovers (juce::Graphics&);
    void beginGlobalGesture (int globalParam);

    GKDistProcessor& proc;
    juce::Rectangle<float> plot;
    SpectrumAnalyzer pre, post;
    double animTime = 0.0;
    juce::Image staticLayer;   // background gradient + grid + labels, rendered once per size
    PaintBudget budget;        // repaint rate adapts to the paint cost

    BandParams bands[kNumBands];
    dsp::BandTelemetry tel[kNumBands];
    std::array<float, kNumBands> liveDrive {}, intensity {}, activity {};
    // Displayed (live, smoothed) values: curves glide with dynamics and modulation at any frame rate.
    std::array<float, kNumBands> dispLogF {}, dispLogQ {}, dispFiltLogF {}, dispFiltLogR {}, dispFiltGain {};
    std::array<bool, kNumBands> dispInit {};
    struct Region
    {
        bool valid = false;
        double key[5] {};
        std::array<float, kSpecPoints> mag {};   // 0..1 region magnitude
    };
    std::array<Region, kNumBands> regions;
    double regionRate = 0.0;

    // Colour mix and clashes per analyser point
    std::array<juce::Colour, kSpecPoints> mixColour;
    std::array<float, kSpecPoints> mixTop {}, mixAlpha {}, clashAmt {}, clashTop {};
    std::array<std::array<float, kNumBands>, kNumBands> clash {};

    // Selected band: filter response (dB) and detector bell (0..1)
    struct Curve
    {
        bool valid = false;
        double key[6] {};
        std::array<float, kSpecPoints> v {};
    };
    Curve filterCurve, detCurve;

    int selected = -1;
    Hit hover, drag;
    juce::Point<float> dragStart, mousePos;
    bool mouseInside = false;
    double startFreq = 1000.0, startDrive = 12.0, startQ = 1.0, startRes = 0.707, startGain = 0.0, startThresh = -30.0;
    std::vector<juce::RangedAudioParameter*> gestureParams;
    double wheelCommit = 0.0;   // seconds until a wheel gesture is committed to undo
    FrameClock clock { *this, [this] (double dt) { frame (dt); } };   // last: stops first
};

} // namespace gkd::ui
