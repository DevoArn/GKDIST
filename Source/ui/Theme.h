// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Params.h"

namespace gkd::ui
{

/** Colours of the current theme. They are set by applyTheme(); the editor
    rebuilds itself when the theme changes, so every component picks them up. */
namespace Colours
{
    inline juce::Colour bg0        { 0xff110c0a };
    inline juce::Colour bg1        { 0xff19120f };
    inline juce::Colour bg2        { 0xff211814 };
    inline juce::Colour panel      { 0xe6241a15 };
    inline juce::Colour panelEdge  { 0xff3d2c23 };
    inline juce::Colour grid       { 0x14ffe3cc };
    inline juce::Colour gridStrong { 0x28ffe3cc };
    inline juce::Colour text       { 0xfff3e6da };
    inline juce::Colour textDim    { 0xffa38f80 };
    inline juce::Colour accent     { 0xffff9a3d };
    inline juce::Colour accent2    { 0xffff5e7a };
    inline juce::Colour curve      { 0xfffff3e6 };
    inline juce::Colour preSpec    { 0xff4d3a30 };
    inline juce::Colour postSpec   { 0xffc9967a };
    inline juce::Colour scSpec     { 0xffffc04d };
    inline juce::Colour instSpec   { 0xffe08bff };
    inline juce::Colour collision  { 0xffff3b5c };
    inline juce::Colour on         { 0xff8fd16a };
    inline juce::Colour warn       { 0xffffc04d };
    inline juce::Colour knobHi     { 0xff3a2c25 };
    inline juce::Colour knobLo     { 0xff1c1411 };
}

/** Colour themes. Ember (warm) is the default; Midnight is the original cool look. */
enum class ThemeId : int { Ember = 0, Sunset, Vintage, Midnight, Count };
juce::StringArray themeNames();
/** Sets the Colours and the band palette of a theme. */
void applyTheme (ThemeId t);
ThemeId currentTheme() noexcept;

/** The user's theme choice, shared by all plug-in instances and saved in the
    user settings (%APPDATA%\GKDIST\GKDIST.settings). Use it through a
    juce::SharedResourcePointer; editors listen for changes. */
class ThemeManager : public juce::ChangeBroadcaster
{
public:
    ThemeManager();
    ThemeId get() const noexcept { return theme; }
    /** Applies a theme and notifies all editors; persist = save as the user's choice. */
    void set (ThemeId t, bool persist = true);

private:
    std::unique_ptr<juce::PropertiesFile> settings;
    ThemeId theme = ThemeId::Ember;
};

/** Distinct colour per band from the theme's palette. */
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
    /** Overall text size (1.0 = the original design size). */
    static constexpr float kTextScale = 1.15f;
};

} // namespace gkd::ui
