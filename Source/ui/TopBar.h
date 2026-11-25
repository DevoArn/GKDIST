// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Theme.h"

namespace gkd::ui
{

class TopBar : public juce::Component, private juce::Timer
{
public:
    explicit TopBar (GKDistProcessor& p);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onStateLoaded;
    std::function<void()> onGpuToggled;
    std::function<void (int, int)> onResizeRequest;

private:
    struct PresetRef { bool factory = true; int index = 0; juce::File file; juce::String name; };

    void timerCallback() override;
    std::vector<PresetRef> allPresets() const;
    void loadPreset (const PresetRef& r);
    void showPresetMenu();
    void stepPreset (int dir);
    void savePreset();
    void showSettingsMenu();
    void showHelp();

    GKDistProcessor& proc;
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, aBtn { "A" }, bBtn { "B" }, copyBtn { "Copy A>B" },
                     prevPreset { "<" }, nextPreset { ">" }, presetBtn { "Default" }, saveBtn { "Save" },
                     settingsBtn { "Settings" }, helpBtn { "?" };
    std::unique_ptr<juce::AlertWindow> saveDialog;
    juce::Rectangle<int> infoArea;
};

} // namespace gkd::ui
