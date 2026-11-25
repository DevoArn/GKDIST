// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace gkd::ui
{

/** Rotary knob: caption above, editable value below, double-click resets. */
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& caption);

    void resized() override;
    void paint (juce::Graphics&) override;

    void attach (juce::RangedAudioParameter* p);
    void setAccent (juce::Colour c);
    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }

    juce::Slider slider;
    std::function<void()> onEdit;

private:
    juce::String caption;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

/** Text toggle bound to a bool parameter. */
class ToggleBox : public juce::TextButton
{
public:
    explicit ToggleBox (const juce::String& text);
    void attach (juce::RangedAudioParameter* p);
    void setOnColour (juce::Colour c) { setColour (juce::TextButton::buttonOnColourId, c); }
    std::function<void()> onEdit;

private:
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

/** Combo box bound to a choice parameter. */
class ChoiceBox : public juce::ComboBox
{
public:
    explicit ChoiceBox (const juce::StringArray& items);
    void attach (juce::RangedAudioParameter* p);
    std::function<void()> onEdit;

private:
    std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
};

/** Static transfer curve of a distortion setting (input -1..1 -> output). */
class TransferCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (DistType type, double driveDb, double bias, bool autoGain, juce::Colour colour);
    void paint (juce::Graphics&) override;

private:
    DistType type = DistType::Tube;
    double driveDb = 12.0, bias = 0.0;
    bool autoGain = true;
    juce::Colour colour = Colours::accent;
};

/** Small horizontal meter (gain reduction / dynamics amount). */
class Meter : public juce::Component
{
public:
    void setValue (float v01, const juce::String& text);
    void setColour (juce::Colour c) { colour = c; }
    void paint (juce::Graphics&) override;

private:
    float value = 0.0f;
    juce::String label;
    juce::Colour colour = Colours::warn;
};

} // namespace gkd::ui
