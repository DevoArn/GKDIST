// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginEditor.h"

namespace gkd
{

GKDistEditor::GKDistEditor (GKDistProcessor& p)
    : AudioProcessorEditor (p), proc (p), top (p), display (p), bandPanel (p), globalPanel (p)
{
    addAndMakeVisible (top);
    addAndMakeVisible (display);
    addAndMakeVisible (bandPanel);
    addAndMakeVisible (globalPanel);
    setLookAndFeel (&lnf);   // after adding children so all of them receive the change

    display.onSelectionChanged = [this] { bandPanel.setBand (display.getSelectedBand()); };
    display.onEdit = [this] { commit(); };
    bandPanel.onEdit = [this] { commit(); };
    bandPanel.onSelectBand = [this] (int b) { display.selectBand (b); };
    globalPanel.onEdit = [this] { commit(); };
    top.onStateLoaded = [this] { display.selectBand (-1); };
    top.onGpuToggled = [this] { setGpu ((bool) proc.uiState.getProperty ("gpu", true)); };
    top.onResizeRequest = [this] (int w, int h) { setSize (w, h); };

    addMouseListener (this, true);
    tooltip = std::make_unique<juce::TooltipWindow> (this, 700);

    // Read the stored size first: setResizeLimits() resizes the editor, and
    // resized() writes the current size back into the UI state.
    const int w = juce::jlimit (kMinWidth, 2560, (int) proc.uiState.getProperty ("width", 1280));
    const int h = juce::jlimit (kMinHeight, 1600, (int) proc.uiState.getProperty ("height", 860));
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 2560, 1600);
    setSize (w, h);
    setWantsKeyboardFocus (true);

    proc.commitUndoPoint();   // undo baseline
    setGpu ((bool) proc.uiState.getProperty ("gpu", true));
}

GKDistEditor::~GKDistEditor()
{
    removeMouseListener (this);
    proc.clearAllSolos();
    if (gl.isAttached()) gl.detach();
    tooltip.reset();
    setLookAndFeel (nullptr);
}

void GKDistEditor::setGpu (bool on)
{
    if (on && ! gl.isAttached())
    {
        gl.setComponentPaintingEnabled (true);
        gl.setContinuousRepainting (false);
        gl.attachTo (*this);
    }
    else if (! on && gl.isAttached())
    {
        gl.detach();
    }
}

void GKDistEditor::paint (juce::Graphics& g) { g.fillAll (ui::Colours::bg0); }

void GKDistEditor::resized()
{
    auto r = getLocalBounds();
    top.setBounds (r.removeFromTop (36));
    globalPanel.setBounds (r.removeFromBottom (ui::GlobalPanel::kHeight));
    bandPanel.setBounds (r.removeFromBottom (ui::BandPanel::kHeight));
    display.setBounds (r);
    proc.uiState.setProperty ("width", getWidth(), nullptr);
    proc.uiState.setProperty ("height", getHeight(), nullptr);
}

bool GKDistEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (mods.isCtrlDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) proc.redo(); else proc.undo();
        return true;
    }
    if (mods.isCtrlDown() && (code == 'Y' || code == 'y'))
    {
        proc.redo();
        return true;
    }
    return false;
}

void GKDistEditor::mouseUp (const juce::MouseEvent&) { commit(); }

} // namespace gkd
