// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TopBar.h"
#include "../Presets.h"

namespace gkd::ui
{

TopBar::TopBar (GKDistProcessor& p) : proc (p)
{
    for (auto* b : { &undoBtn, &redoBtn, &aBtn, &bBtn, &copyBtn, &prevPreset, &nextPreset, &presetBtn, &saveBtn, &settingsBtn, &helpBtn })
        addAndMakeVisible (b);
    undoBtn.setTooltip ("Undo (Ctrl+Z)");
    redoBtn.setTooltip ("Redo (Ctrl+Y / Ctrl+Shift+Z)");
    copyBtn.setTooltip ("Copy the active setting to the other A/B slot");

    undoBtn.onClick = [this] { proc.undo(); if (onStateLoaded) onStateLoaded(); };
    redoBtn.onClick = [this] { proc.redo(); if (onStateLoaded) onStateLoaded(); };
    aBtn.onClick = [this] { if (proc.getABSlot() != 0) { proc.switchAB(); if (onStateLoaded) onStateLoaded(); } };
    bBtn.onClick = [this] { if (proc.getABSlot() != 1) { proc.switchAB(); if (onStateLoaded) onStateLoaded(); } };
    copyBtn.onClick = [this] { proc.copyToOtherSlot(); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    saveBtn.onClick = [this] { savePreset(); };
    settingsBtn.onClick = [this] { showSettingsMenu(); };
    helpBtn.onClick = [this] { showHelp(); };
    startTimerHz (8);
}

TopBar::~TopBar() { stopTimer(); }

void TopBar::timerCallback()
{
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    aBtn.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);
    copyBtn.setButtonText (proc.getABSlot() == 0 ? "Copy A>B" : "Copy B>A");
    if (presetBtn.getButtonText() != proc.currentPresetName) presetBtn.setButtonText (proc.currentPresetName);
    repaint (infoArea);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg2);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.setFont (LookAndFeel::font (17.0f, true));
    const float wGk = g.getCurrentFont().getStringWidthFloat ("GK");
    g.setColour (Colours::text);
    g.drawText ("GK", juce::Rectangle<float> (12.0f, 0.0f, wGk + 2.0f, (float) getHeight()), juce::Justification::centredLeft);
    g.setColour (Colours::warn);
    g.drawText ("DIST", juce::Rectangle<float> (13.0f + wGk, 0.0f, 60.0f, (float) getHeight()), juce::Justification::centredLeft);

    g.setFont (LookAndFeel::font (11.5f));
    const int lat = proc.getActiveLatency();
    const double fs = proc.getCurrentSampleRate();
    juce::String info = "Latency " + juce::String (lat) + " smp";
    if (lat > 0 && fs > 0.0) info << " (" << juce::String (1000.0 * lat / fs, 2) << " ms)";
    const float pk = proc.getOutputPeakDb();
    info << "   Peak " << (pk < -99.0f ? juce::String ("-inf") : juce::String (pk, 1)) << " dB";
    g.setColour (pk > -0.1f ? Colours::collision : Colours::textDim);
    g.drawText (info, infoArea, juce::Justification::centredRight);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (8, 5);
    r.removeFromLeft (84);
    undoBtn.setBounds (r.removeFromLeft (54)); r.removeFromLeft (4);
    redoBtn.setBounds (r.removeFromLeft (54)); r.removeFromLeft (14);
    aBtn.setBounds (r.removeFromLeft (28)); r.removeFromLeft (2);
    bBtn.setBounds (r.removeFromLeft (28)); r.removeFromLeft (4);
    copyBtn.setBounds (r.removeFromLeft (72)); r.removeFromLeft (14);
    helpBtn.setBounds (r.removeFromRight (28)); r.removeFromRight (4);
    settingsBtn.setBounds (r.removeFromRight (72)); r.removeFromRight (8);
    infoArea = r.removeFromRight (250);
    r.removeFromRight (8);
    prevPreset.setBounds (r.removeFromLeft (28)); r.removeFromLeft (2);
    saveBtn.setBounds (r.removeFromRight (54)); r.removeFromRight (6);
    nextPreset.setBounds (r.removeFromRight (28)); r.removeFromRight (2);
    presetBtn.setBounds (r);
}

std::vector<TopBar::PresetRef> TopBar::allPresets() const
{
    std::vector<PresetRef> v;
    const auto names = presets::factoryNames();
    for (int i = 0; i < names.size(); ++i) v.push_back ({ true, i, {}, names[i] });
    for (auto& f : presets::listUserPresets()) v.push_back ({ false, 0, f, f.getFileNameWithoutExtension() });
    return v;
}

void TopBar::loadPreset (const PresetRef& r)
{
    const juce::ValueTree state = r.factory ? presets::makeFactory (r.index, proc.getDefaultParameterState())
                                            : presets::loadUserPreset (r.file);
    if (! state.isValid()) return;
    proc.loadParameterState (state, true);
    proc.currentPresetName = r.name;
    presetBtn.setButtonText (r.name);
    if (onStateLoaded) onStateLoaded();
}

void TopBar::showPresetMenu()
{
    const auto list = allPresets();
    juce::PopupMenu m;
    m.addSectionHeader ("Factory");
    bool anyUser = false;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].factory && ! anyUser) { m.addSeparator(); m.addSectionHeader ("User"); anyUser = true; }
        m.addItem (i + 1, list[(size_t) i].name, true, list[(size_t) i].name == proc.currentPresetName);
    }
    m.addSeparator();
    m.addItem (9000, "Save Preset...");
    m.addItem (9001, "Open Preset Folder");
    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [safe, list] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 9000) { safe->savePreset(); return; }
        if (r == 9001) { presets::userFolder().startAsProcess(); return; }
        if (r >= 1 && r <= (int) list.size()) safe->loadPreset (list[(size_t) (r - 1)]);
    });
}

void TopBar::stepPreset (int dir)
{
    const auto list = allPresets();
    if (list.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i].name == proc.currentPresetName) cur = i;
    const int n = (int) list.size();
    loadPreset (list[(size_t) (cur < 0 ? 0 : (cur + dir + n) % n)]);
}

void TopBar::savePreset()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save Preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->setLookAndFeel (&getLookAndFeel());
    saveDialog->addTextEditor ("name", proc.currentPresetName == "Default" ? juce::String ("My Preset") : proc.currentPresetName);
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TopBar> safe (this);
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->saveDialog == nullptr) return;
        const auto name = safe->saveDialog->getTextEditorContents ("name").trim();
        if (result == 1 && name.isNotEmpty() && presets::saveUserPreset (safe->proc.apvts.copyState(), name))
            safe->proc.currentPresetName = juce::File::createLegalFileName (name);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->saveDialog.reset(); });
    }), false);
}

void TopBar::showSettingsMenu()
{
    auto& ui = proc.uiState;
    juce::PopupMenu m, range, res, speed, tilt, sizes;
    m.addItem (1, "Pre Spectrum", true, (bool) ui.getProperty ("anaPre", true));
    m.addItem (2, "Post Spectrum", true, (bool) ui.getProperty ("anaPost", true));
    m.addItem (3, "Freeze Analyser", true, (bool) ui.getProperty ("anaFreeze", false));
    const int ranges[] = { 60, 90, 120 };
    for (int i = 0; i < 3; ++i) range.addItem (10 + i, juce::String (ranges[i]) + " dB", true, (int) ui.getProperty ("anaRange", 90) == ranges[i]);
    const char* resNames[] = { "Low", "Medium", "High", "Maximum" };
    for (int i = 0; i < 4; ++i) res.addItem (20 + i, resNames[i], true, (int) ui.getProperty ("anaRes", 2) == i);
    const char* speedNames[] = { "Very Slow", "Slow", "Medium", "Fast", "Very Fast" };
    for (int i = 0; i < 5; ++i) speed.addItem (30 + i, speedNames[i], true, (int) ui.getProperty ("anaSpeed", 2) == i);
    const double tilts[] = { 0.0, 1.5, 3.0, 4.5, 6.0 };
    for (int i = 0; i < 5; ++i) tilt.addItem (40 + i, juce::String (tilts[i], 1) + " dB/oct", true, std::abs ((double) ui.getProperty ("anaTilt", 4.5) - tilts[i]) < 0.01);
    m.addSubMenu ("Analyser Range", range);
    m.addSubMenu ("Analyser Resolution", res);
    m.addSubMenu ("Analyser Speed", speed);
    m.addSubMenu ("Analyser Tilt", tilt);
    m.addSeparator();
    sizes.addItem (50, "Normal (1280 x 900)");
    sizes.addItem (51, "Large (1600 x 1040)");
    sizes.addItem (52, "Extra Large (1920 x 1200)");
    m.addSubMenu ("Window Size", sizes);
    m.addItem (4, "GPU Accelerated Drawing (OpenGL)", true, (bool) ui.getProperty ("gpu", true));

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& u = safe->proc.uiState;
        auto flip = [&u] (const char* id, bool def) { u.setProperty (id, ! (bool) u.getProperty (id, def), nullptr); };
        const int ranges[] = { 60, 90, 120 };
        const double tilts[] = { 0.0, 1.5, 3.0, 4.5, 6.0 };
        if (r == 1) flip ("anaPre", true);
        if (r == 2) flip ("anaPost", true);
        if (r == 3) flip ("anaFreeze", false);
        if (r == 4) { flip ("gpu", true); if (safe->onGpuToggled) safe->onGpuToggled(); }
        if (r >= 10 && r < 13) u.setProperty ("anaRange", ranges[r - 10], nullptr);
        if (r >= 20 && r < 24) u.setProperty ("anaRes", r - 20, nullptr);
        if (r >= 30 && r < 35) u.setProperty ("anaSpeed", r - 30, nullptr);
        if (r >= 40 && r < 45) u.setProperty ("anaTilt", tilts[r - 40], nullptr);
        const int w[] = { 1280, 1600, 1920 }, h[] = { 900, 1040, 1200 };
        if (r >= 50 && r < 53 && safe->onResizeRequest) safe->onResizeRequest (w[r - 50], h[r - 50]);
    });
}

void TopBar::showHelp()
{
    const juce::String text =
        "Bands\n"
        "  Double-click the display: new band (Bell). Right-click: other shapes.\n"
        "  Drag a node: frequency (left/right) and drive (up/down).\n"
        "  Shift+drag: fine.  Ctrl+drag: Q.  Wheel: Q.  Ctrl+wheel: wet.\n"
        "  Double-click a node: enable/bypass.  Alt+click: delete.\n"
        "  Each band takes its region of the signal (Bell, Low, High or Full),\n"
        "  processes it (filter, distortion, phat, noise, compressor, clipper)\n"
        "  and mixes it back with Wet and Gain.\n"
        "\nDynamics\n"
        "  The band level (or the external side-chain) is compared with the\n"
        "  threshold; over 12 dB above it the band's gain, drive and wet move by\n"
        "  their +/- ranges.\n"
        "\nGlobal\n"
        "  Oversampling reduces aliasing. The limiter guarantees the ceiling.\n"
        "\nKeyboard\n"
        "  Delete: remove band   Esc: deselect   Arrows: frequency / drive\n"
        "  Ctrl+Z / Ctrl+Y: undo / redo\n";
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "GKDIST - Quick Reference", text, "OK", this);
}

} // namespace gkd::ui
