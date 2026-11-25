// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"

namespace gkd::presets
{

/** Edits a parameter tree (as produced by APVTS::copyState). */
class StateBuilder
{
public:
    explicit StateBuilder (juce::ValueTree defaults) : state (std::move (defaults)) {}

    StateBuilder& set (const juce::String& id, float value);
    StateBuilder& band (int b, int index, float value) { return set (bandParamId (b, index), value); }
    StateBuilder& global (int index, float value) { return set (globalParamId (index), value); }
    /** Enables band b with a region and distortion. */
    StateBuilder& add (int b, Shape shape, double freq, double q, DistType type, double driveDb, double wetPercent = 100.0);

    juce::ValueTree get() const { return state; }

private:
    juce::ValueTree state;
};

juce::StringArray factoryNames();
juce::ValueTree   makeFactory (int index, const juce::ValueTree& defaults);

juce::File               userFolder();
juce::Array<juce::File>  listUserPresets();
bool                     saveUserPreset (const juce::ValueTree& params, const juce::String& name);
juce::ValueTree          loadUserPreset (const juce::File& file);

} // namespace gkd::presets
