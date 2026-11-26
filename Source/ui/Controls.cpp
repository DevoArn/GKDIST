// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Controls.h"
#include "../dsp/Shapers.h"
#include "../dsp/FilterDesign.h"
#include <cstring>

namespace gkd::ui
{

Knob::Knob (const juce::String& c) : caption (c)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    resized();
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (compact ? 12 : 15);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? Colours::textDim : Colours::textDim.withAlpha (0.4f));
    g.setFont (LookAndFeel::font (compact ? 9.5f : 10.5f, true));
    g.drawFittedText (caption, getLocalBounds().removeFromTop (compact ? 12 : 15), juce::Justification::centred, 1, 0.75f);
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
    if (type != DistType::PhaseDist)
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 0.8f);   // unity

    if (type == DistType::PhaseDist)
    {
        // Phase distortion is not a level curve: show one cycle of a sine (grey)
        // and what the phase warp does to it (same model as the PhaseDistorter,
        // for a 110 Hz tone).
        const double t01 = juce::jlimit (0.0, 1.0, driveDb / 48.0);
        const double depthCycles = t01 * t01 * 0.004 * 110.0;
        juce::Path ref, warped;
        const int steps = juce::jmax (48, (int) r.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const double ph = (double) i / steps;
            const double s = std::sin (6.283185307179586 * ph);
            const double m = std::tanh (2.0 * 0.8 * std::sin (6.283185307179586 * ph - 0.3 * (1.0 + bias)));
            const double w = std::sin (6.283185307179586 * (ph - depthCycles * (1.0 + m)));
            const float px = r.getX() + (float) ph * r.getWidth();
            const float y0 = r.getCentreY() - (float) (s / 1.1) * r.getHeight() * 0.5f;
            const float y1 = r.getCentreY() - (float) (w / 1.1) * r.getHeight() * 0.5f;
            if (i == 0) { ref.startNewSubPath (px, y0); warped.startNewSubPath (px, y1); }
            else        { ref.lineTo (px, y0); warped.lineTo (px, y1); }
        }
        g.setColour (Colours::textDim.withAlpha (0.5f));
        g.strokePath (ref, juce::PathStrokeType (1.0f));
        g.setColour (colour);
        g.strokePath (warped, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

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
void ResponseCurve::set (const BandParams& p, double sampleRate, juce::Colour c)
{
    const double k[7] { (double) p.filterType, (double) p.filterSlope, p.filterFreq, p.filterRes, p.filterGainDb, sampleRate,
                        (double) c.getARGB() };
    if (std::memcmp (k, key, sizeof (k)) == 0) return;
    std::memcpy (key, k, sizeof (k));
    colour = c;
    on = p.filterType != BandFilter::Off;
    if (on)
    {
        dsp::DesignParams d;
        d.type = bandFilterDesignType (p.filterType);
        d.freq = p.filterFreq;
        d.q = p.filterRes;
        d.gainDb = p.filterGainDb;
        d.slopeIdx = regionSlopeDesignIdx (p.filterSlope);
        const auto a = dsp::designAnalog (d, sampleRate);
        for (int i = 0; i < kPoints; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / (double) (kPoints - 1));
            db[(size_t) i] = (float) dsp::gainToDb (a.mag (dsp::kTwoPi * std::min (f, 0.499 * sampleRate) / sampleRate));
        }
    }
    repaint();
}

void ResponseCurve::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::bg0);
    g.fillRoundedRectangle (r, 4.0f);
    const auto in = r.reduced (4.0f, 6.0f);
    auto yOf = [&in] (float v) { return in.getCentreY() - juce::jlimit (-24.0f, 24.0f, v) / 24.0f * 0.5f * in.getHeight(); };
    g.setColour (Colours::grid);
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = in.getX() + in.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
    }
    for (float v : { -12.0f, 12.0f }) g.drawHorizontalLine ((int) yOf (v), in.getX(), in.getRight());
    g.setColour (Colours::gridStrong);
    g.drawHorizontalLine ((int) yOf (0.0f), in.getX(), in.getRight());
    g.setFont (LookAndFeel::font (9.5f));
    g.setColour (Colours::textDim);
    g.drawText ("100", juce::Rectangle<float> (in.getX() + in.getWidth() / 3.0f - 14.0f, in.getBottom() - 11.0f, 28.0f, 11.0f), juce::Justification::centred);
    g.drawText ("1k", juce::Rectangle<float> (in.getX() + in.getWidth() * 2.0f / 3.0f - 14.0f, in.getBottom() - 11.0f, 28.0f, 11.0f), juce::Justification::centred);
    if (! on)
    {
        g.drawText ("filter off", in, juce::Justification::centred);
        return;
    }
    juce::Path p;
    for (int i = 0; i < kPoints; ++i)
    {
        const float x = in.getX() + in.getWidth() * (float) i / (float) (kPoints - 1);
        const float y = yOf (db[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path fill (p);
    fill.lineTo (in.getRight(), yOf (0.0f));
    fill.lineTo (in.getX(), yOf (0.0f));
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.8f));
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
