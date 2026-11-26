// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Theme.h"

namespace gkd::ui
{

namespace
{
    struct ThemeDef
    {
        juce::uint32 bg0, bg1, bg2, panel, panelEdge, grid, gridStrong, text, textDim, accent, accent2, curve,
                     preSpec, postSpec, scSpec, instSpec, collision, on, warn, knobHi, knobLo;
        juce::uint32 palette[12];   // 0 = generated golden-ratio hues
    };

    const ThemeDef kThemes[] = {
        // Ember: charcoal brown, amber accent, fire / gold / rose bands
        { 0xff110c0a, 0xff19120f, 0xff211814, 0xe6241a15, 0xff3d2c23, 0x14ffe3cc, 0x28ffe3cc, 0xfff3e6da, 0xffa38f80,
          0xffff9a3d, 0xffff5e7a, 0xfffff3e6, 0xff4d3a30, 0xffc9967a, 0xffffc04d, 0xffe08bff, 0xffff3b5c, 0xff8fd16a,
          0xffffc04d, 0xff3a2c25, 0xff1c1411,
          { 0xffff8a3d, 0xffffc247, 0xffff5d5d, 0xffe86aa6, 0xffffa77a, 0xffd9a441, 0xffff7350, 0xffc65a7c,
            0xfff2d17c, 0xffe0663a, 0xffff9fb4, 0xffc2c65a } },
        // Sunset: plum night, pink / orange / violet bands
        { 0xff130b14, 0xff1b111d, 0xff241726, 0xe6261828, 0xff422a47, 0x14ffd9f0, 0x28ffd9f0, 0xfff6e6f0, 0xffa98ea3,
          0xffff6f91, 0xffffb057, 0xfffff0f6, 0xff45304d, 0xffc98fb0, 0xffffb057, 0xffc77dff, 0xffff3b3b, 0xff7fd8a0,
          0xffffc04d, 0xff3c2840, 0xff1a101c,
          { 0xffff6f91, 0xffffa45c, 0xffc77dff, 0xffffd166, 0xffff8fab, 0xffef476f, 0xfff9a03f, 0xffb185db,
            0xffff5c8a, 0xffffc3a0, 0xffd66ba0, 0xfff4845f } },
        // Vintage: tube amp - dark olive, brass accent, cream / copper / sage bands
        { 0xff121009, 0xff1a1810, 0xff222017, 0xe6242219, 0xff403b2a, 0x14fff2cc, 0x28fff2cc, 0xfff1e9d0, 0xffa59d82,
          0xffe8b04a, 0xffd8643a, 0xfffff8e6, 0xff45402f, 0xffbfae85, 0xffe8b04a, 0xffc9a0dc, 0xffff4b3b, 0xff9fc46a,
          0xffffc857, 0xff38342a, 0xff1a1812,
          { 0xffe8b04a, 0xffd8643a, 0xffeedc9a, 0xffa9b45a, 0xffc8794a, 0xffe0524a, 0xffd6b23a, 0xff7fae8f,
            0xffd98a8a, 0xfff0923c, 0xffc9b98a, 0xffb5524f } },
        // Midnight: the original cool blue look, rainbow bands
        { 0xff0b0e13, 0xff11151c, 0xff181d26, 0xe61a202a, 0xff2b3344, 0x14ffffff, 0x26ffffff, 0xffd9dfea, 0xff7f8a9e,
          0xff4fc3ff, 0xffc58bff, 0xfff4f6fa, 0xff3a4a66, 0xff7d9cc9, 0xffff9f43, 0xffc58bff, 0xffff4d5e, 0xff35d07f,
          0xffffc04d, 0xff2a3140, 0xff151a22,
          { 0 } },
    };
    static_assert (sizeof (kThemes) / sizeof (kThemes[0]) == (size_t) ThemeId::Count, "theme table out of sync");

    ThemeId current = ThemeId::Ember;
    std::array<juce::Colour, kNumBands> palette;

    void buildPalette (const ThemeDef& t)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            if (t.palette[0] == 0)
            {
                const float hue = std::fmod (0.03f + (float) b * 0.61803398875f, 1.0f);
                palette[(size_t) b] = juce::Colour::fromHSV (hue, 0.62f, 0.98f, 1.0f);
                continue;
            }
            const juce::Colour base (t.palette[b % 12]);
            if (b < 12) { palette[(size_t) b] = base; continue; }
            // Bands 13 - 24: same family, shifted hue and lighter, so neighbours stay distinct.
            float h, s, v;
            base.getHSB (h, s, v);
            palette[(size_t) b] = juce::Colour::fromHSV (std::fmod (h + 0.035f, 1.0f), s * 0.72f, std::min (1.0f, v * 1.08f + 0.05f), 1.0f);
        }
    }

    bool paletteReady = false;
}

juce::StringArray themeNames() { return { "Ember (warm)", "Sunset (warm)", "Vintage (warm)", "Midnight (cool)" }; }

ThemeId currentTheme() noexcept { return current; }

void applyTheme (ThemeId id)
{
    const int i = juce::jlimit (0, (int) ThemeId::Count - 1, (int) id);
    current = (ThemeId) i;
    const auto& t = kThemes[i];
    using namespace Colours;
    bg0 = juce::Colour (t.bg0); bg1 = juce::Colour (t.bg1); bg2 = juce::Colour (t.bg2); panel = juce::Colour (t.panel);
    panelEdge = juce::Colour (t.panelEdge); grid = juce::Colour (t.grid); gridStrong = juce::Colour (t.gridStrong);
    text = juce::Colour (t.text); textDim = juce::Colour (t.textDim); accent = juce::Colour (t.accent);
    accent2 = juce::Colour (t.accent2); curve = juce::Colour (t.curve); preSpec = juce::Colour (t.preSpec);
    postSpec = juce::Colour (t.postSpec); scSpec = juce::Colour (t.scSpec); instSpec = juce::Colour (t.instSpec);
    collision = juce::Colour (t.collision); on = juce::Colour (t.on); warn = juce::Colour (t.warn);
    knobHi = juce::Colour (t.knobHi); knobLo = juce::Colour (t.knobLo);
    buildPalette (t);
    paletteReady = true;
}

ThemeManager::ThemeManager()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "GKDIST";
    o.folderName = "GKDIST";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    settings = std::make_unique<juce::PropertiesFile> (o);
    // The settings file may have been edited by hand: clamp.
    theme = (ThemeId) juce::jlimit (0, (int) ThemeId::Count - 1, settings->getIntValue ("theme", 0));
    applyTheme (theme);
}

void ThemeManager::set (ThemeId t, bool persist)
{
    t = (ThemeId) juce::jlimit (0, (int) ThemeId::Count - 1, (int) t);
    if (persist && settings != nullptr)
    {
        settings->setValue ("theme", (int) t);
        settings->saveIfNeeded();
    }
    if (t == theme && currentTheme() == t) return;
    theme = t;
    applyTheme (t);
    sendSynchronousChangeMessage();
}

juce::Colour bandColour (int band)
{
    if (! paletteReady) applyTheme (current);
    return palette[(size_t) juce::jlimit (0, kNumBands - 1, band)];
}

juce::Colour intensityColour (int band, float intensity)
{
    const float i = juce::jlimit (0.0f, 1.0f, intensity);
    float h, s, v;
    bandColour (band).getHSB (h, s, v);
    // Gentle settings are pale and dim, hard-driven bands fully saturated and bright.
    return juce::Colour::fromHSV (h, s * (0.3f + 0.7f * i), std::min (1.0f, v * (0.62f + 0.42f * i)), 1.0f);
}

juce::Path shapeIcon (Shape s, juce::Rectangle<float> r)
{
    juce::Path p;
    const float x0 = r.getX(), x1 = r.getRight(), w = r.getWidth();
    const float top = r.getY(), bot = r.getBottom();
    auto at = [&] (float f) { return x0 + f * w; };
    switch (s)
    {
        case Shape::Bell:
            p.startNewSubPath (x0, bot);
            p.lineTo (at (0.25f), bot);
            p.quadraticTo (at (0.5f), top - (bot - top) * 0.6f, at (0.75f), bot);
            p.lineTo (x1, bot);
            break;
        case Shape::Low:
            p.startNewSubPath (x0, top + 1.0f);
            p.lineTo (at (0.45f), top + 1.0f);
            p.quadraticTo (at (0.65f), top + 1.0f, at (0.85f), bot);
            break;
        case Shape::High:
            p.startNewSubPath (at (0.15f), bot);
            p.quadraticTo (at (0.35f), top + 1.0f, at (0.55f), top + 1.0f);
            p.lineTo (x1, top + 1.0f);
            break;
        case Shape::Full:
        case Shape::Count:
        default:
            p.startNewSubPath (x0, top + 1.0f);
            p.lineTo (x1, top + 1.0f);
            break;
    }
    return p;
}

// ---------------------------------------------------------------------------
LookAndFeel::LookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");

    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg0);
    setColour (juce::PopupMenu::backgroundColourId, Colours::bg2);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, Colours::textDim);
    setColour (juce::ComboBox::backgroundColourId, Colours::bg2);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::outlineColourId, Colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Colours::textDim);
    setColour (juce::TextButton::buttonColourId, Colours::bg2);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, Colours::bg1);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::outlineColourId, Colours::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    setColour (juce::TextEditor::highlightColourId, Colours::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::bg2);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::backgroundColourId, Colours::bg2);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::panelEdge);
    setColour (juce::ToggleButton::textColourId, Colours::text);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::textDim);
    setColour (juce::ScrollBar::thumbColourId, Colours::panelEdge);
}

juce::Font LookAndFeel::font (float h, bool bold)
{
    // All text in GKDIST goes through here: one place sets the overall text size.
    return juce::Font ("Segoe UI", h * kTextScale, bold ? juce::Font::bold : juce::Font::plain);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = s.isEnabled();

    // Track
    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (Colours::panelEdge);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc (bipolar sliders start from the centre)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? accent : Colours::textDim);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body
    const float br = radius - 6.0f;
    juce::ColourGradient grad (Colours::knobHi, c.x, c.y - br, Colours::knobLo, c.x, c.y + br, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colour (0x30ffffff));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    // Pointer
    const float pr = br - 3.0f;
    const juce::Point<float> tip (c.x + pr * std::sin (angle), c.y - pr * std::cos (angle));
    const juce::Point<float> base (c.x + pr * 0.35f * std::sin (angle), c.y - pr * 0.35f * std::cos (angle));
    g.setColour (enabled ? Colours::text : Colours::textDim);
    g.drawLine ({ base, tip }, 2.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = r.getCentreY();
        g.setColour (Colours::panelEdge);
        g.fillRoundedRectangle (r.getX(), cy - 2.0f, r.getWidth(), 4.0f, 2.0f);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.fillRoundedRectangle (r.getX(), cy - 2.0f, pos - r.getX(), 4.0f, 2.0f);
        g.setColour (Colours::text);
        g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        return;
    }
    LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Colours::accent.withAlpha (0.6f) : Colours::panelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
    g.fillPath (arrow);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jlimit (11.0f, 15.0f, (float) box.getHeight() * 0.55f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (over) base = base.brighter (0.12f);
    if (down) base = base.brighter (0.2f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? Colours::accent.withAlpha (0.7f) : Colours::panelEdge);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return font (juce::jlimit (11.0f, 14.5f, (float) h * 0.52f));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawRect (0, 0, w, h, 1);
}

juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (Colours::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 4);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font ((bool) s.getProperties()["compact"] ? 10.5f : 12.5f));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    l->setColour (juce::Label::backgroundWhenEditingColourId, Colours::bg1);
    return l;
}

void LookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (Colours::bg2);
    g.fillPath (path);
    g.setColour (Colours::panelEdge);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

} // namespace gkd::ui
