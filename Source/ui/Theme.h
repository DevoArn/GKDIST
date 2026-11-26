// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Params.h"

namespace gkd::ui
{

namespace Colours
{
    inline const juce::Colour bg0        { 0xff0b0e13 };
    inline const juce::Colour bg1        { 0xff11151c };
    inline const juce::Colour bg2        { 0xff181d26 };
    inline const juce::Colour panel      { 0xe61a202a };
    inline const juce::Colour panelEdge  { 0xff2b3344 };
    inline const juce::Colour grid       { 0x14ffffff };
    inline const juce::Colour gridStrong { 0x26ffffff };
    inline const juce::Colour text       { 0xffd9dfea };
    inline const juce::Colour textDim    { 0xff7f8a9e };
    inline const juce::Colour accent     { 0xff4fc3ff };
    inline const juce::Colour curve      { 0xfff4f6fa };
    inline const juce::Colour preSpec    { 0xff3a4a66 };
    inline const juce::Colour postSpec   { 0xff7d9cc9 };
    inline const juce::Colour scSpec     { 0xffff9f43 };
    inline const juce::Colour instSpec   { 0xffc58bff };
    inline const juce::Colour collision  { 0xffff4d5e };
    inline const juce::Colour on         { 0xff35d07f };
    inline const juce::Colour warn       { 0xffffc04d };
}

/** Distinct, evenly spread hue per band. */
juce::Colour bandColour (int band);
/** Band colour whose saturation and brightness rise with intensity (0..1). */
juce::Colour intensityColour (int band, float intensity);

/** Small vector icon for a band region shape. */
juce::Path shapeIcon (Shape s, juce::Rectangle<float> r);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;

    static juce::Font font (float h, bool bold = false);
};

} // namespace gkd::ui
