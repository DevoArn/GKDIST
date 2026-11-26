// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Controls.h"
#include "../dsp/Shapers.h"

namespace gkd::ui
{

Knob::Knob (const juce::String& c) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    slider.setMouseDragSensitivity (220);
    slider.onDragEnd = [this] { if (onEdit) onEdit(); };
    addAndMakeVisible (slider);
}

void Knob::attach (juce::RangedAudioParameter* p)
{
    attachment.reset();
    if (p == nullptr) return;
    attachment = std::make_unique<juce::SliderParameterAttachment> (*p, slider, nullptr);
    slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
}

void Knob::setCompact()
{
    compact = true;
    slider.getProperties().set ("compact", true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 13);
    resized();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (compact ? 11 : 13);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
    g.setFont (LookAndFeel::font (compact ? 9.5f : 10.5f, true));
    g.drawText (caption, getLocalBounds().removeFromTop (compact ? 11 : 13), juce::Justification::centred);
}

// ---------------------------------------------------------------------------
ToggleBox::ToggleBox (const juce::String& text) : juce::TextButton (text)
{
    setClickingTogglesState (true);
    onClick = [this] { if (onEdit) onEdit(); };
}

void ToggleBox::attach (juce::RangedAudioParameter* p)
{
    attachment.reset();
    if (p != nullptr)
        attachment = std::make_unique<juce::ButtonParameterAttachment> (*p, *this, nullptr);
}

// ---------------------------------------------------------------------------
ChoiceBox::ChoiceBox (const juce::StringArray& items)
{
    addItemList (items, 1);
    onChange = [this]
    {
        juce::Component::SafePointer<ChoiceBox> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onEdit) safe->onEdit(); });
    };
}

void ChoiceBox::attach (juce::RangedAudioParameter* p)
{
    attachment.reset();
    if (p != nullptr)
        attachment = std::make_unique<juce::ComboBoxParameterAttachment> (*p, *this, nullptr);
}

// ---------------------------------------------------------------------------
void TransferCurve::set (DistType t, double d, double b, bool ag, juce::Colour c)
{
    if (t == type && d == driveDb && b == bias && ag == autoGain && c == colour) return;
    type = t; driveDb = d; bias = b; autoGain = ag; colour = c;
    repaint();
}

void TransferCurve::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::bg1);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Colours::grid);
    g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom());
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY());
    g.setColour (Colours::gridStrong);
    g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 0.8f);   // unity

    // The curve shows the shape over an input of -1..+1, scaled so its peak
    // fills the box (auto gain only changes the level, not the shape).
    const double drive = std::pow (10.0, driveDb / 20.0);
    const double bits = dsp::crushBits (driveDb);
    const int steps = juce::jmax (32, (int) r.getWidth());
    std::vector<double> ys ((size_t) steps + 1);
    double peak = 0.0;
    for (int i = 0; i <= steps; ++i)
    {
        const double x = -1.0 + 2.0 * i / steps;
        const double y = type == DistType::Bitcrush ? dsp::crushQuantise (x, bits)
                                                    : dsp::shapeBiased (type, x * drive, bias);
        ys[(size_t) i] = std::isfinite (y) ? y : 0.0;
        peak = juce::jmax (peak, std::abs (ys[(size_t) i]));
    }
    const double norm = peak > 1.0e-9 ? 1.0 / peak : 1.0;
    juce::Path p;
    for (int i = 0; i <= steps; ++i)
    {
        const double y = ys[(size_t) i] * norm;
        const float px = r.getX() + (float) i / (float) steps * r.getWidth();
        const float py = r.getCentreY() - (float) (y / 1.1) * r.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------------------
void Meter::setValue (float v01, const juce::String& text)
{
    v01 = juce::jlimit (0.0f, 1.0f, v01);
    if (std::abs (v01 - value) < 0.002f && text == label) return;
    value = v01;
    label = text;
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bar = r.removeFromBottom (5.0f);
    g.setColour (Colours::bg1);
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * value), 2.0f);
    g.setColour (Colours::textDim);
    g.setFont (LookAndFeel::font (10.5f));
    g.drawText (label, r, juce::Justification::centredLeft);
}

} // namespace gkd::ui
