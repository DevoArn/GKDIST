// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginEditor.h"

namespace gkd
{

EditorContent::EditorContent (GKDistProcessor& p)
    : proc (p), top (p), display (p), wave (p), bandPanel (p), mixerPanel (p), modPanel (p), globalPanel (p)
{
    addAndMakeVisible (top);
    addAndMakeVisible (display);
    addChildComponent (wave);
    addChildComponent (bandPanel);
    addChildComponent (mixerPanel);
    addChildComponent (modPanel);
    addAndMakeVisible (globalPanel);
    for (int i = 0; i < 3; ++i)
    {
        tabs[i].setClickingTogglesState (true);
        tabs[i].setRadioGroupId (0x6d6f64);
        tabs[i].setColour (juce::TextButton::buttonOnColourId, ui::Colours::accent.withAlpha (0.35f));
        tabs[i].onClick = [this, i] { if (tabs[i].getToggleState()) showPage (i); };
        addAndMakeVisible (tabs[i]);   // added last: drawn over the pages' header row
    }
    tabs[0].setTooltip ("Controls of the selected band");
    tabs[1].setTooltip ("Mixer: a channel strip for every band");
    tabs[2].setTooltip ("Modulation: LFOs, envelopes and the modulation matrix");

    auto edit = [this] { if (onEdit) onEdit(); };
    display.onSelectionChanged = [this]
    {
        bandPanel.setBand (display.getSelectedBand());
        mixerPanel.setSelectedBand (display.getSelectedBand());
    };
    display.onEdit = edit;
    bandPanel.onEdit = edit;
    bandPanel.onSelectBand = [this] (int b) { display.selectBand (b); };
    mixerPanel.onEdit = edit;
    mixerPanel.onSelectBand = [this] (int b) { display.selectBand (b); };
    mixerPanel.onOpenBand = [this] (int b) { display.selectBand (b); showPage (0); };
    modPanel.onEdit = edit;
    globalPanel.onEdit = edit;
    top.onStateLoaded = [this] { display.selectBand (-1); };
    top.onGpuToggled = [this] { if (onGpuToggled) onGpuToggled(); };
    top.onResizeRequest = [this] (int w, int h) { if (onResizeRequest) onResizeRequest (w, h); };
    top.onThemeRequest = [this] (ThemeId t) { if (onThemeRequest) onThemeRequest (t); };
    top.onWaveToggled = [this] { resized(); };

    showPage (juce::jlimit (0, 2, (int) proc.uiState.getProperty ("lowerTab", 0)));
}

void EditorContent::showPage (int p)
{
    page = juce::jlimit (0, 2, p);
    bandPanel.setVisible (page == 0);
    mixerPanel.setVisible (page == 1);
    modPanel.setVisible (page == 2);
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState (i == page, juce::dontSendNotification);
    proc.uiState.setProperty ("lowerTab", page, nullptr);
}

void EditorContent::paint (juce::Graphics& g) { g.fillAll (ui::Colours::bg0); }

void EditorContent::resized()
{
    auto r = getLocalBounds();
    top.setBounds (r.removeFromTop (36));
    globalPanel.setBounds (r.removeFromBottom (ui::GlobalPanel::kHeight));
    const auto lower = r.removeFromBottom (ui::BandPanel::kHeight);
    const bool showWave = (bool) proc.uiState.getProperty ("wave", false);
    wave.setVisible (showWave);
    if (showWave) wave.setBounds (r.removeFromBottom (ui::WaveformView::kHeight));
    bandPanel.setBounds (lower);
    mixerPanel.setBounds (lower);
    modPanel.setBounds (lower);
    display.setBounds (r);

    auto tabRow = lower.withHeight (ui::BandPanel::kHeader).reduced (8, 3);
    tabRow = tabRow.removeFromRight (ui::BandPanel::kTabsWidth - 12);
    const int tw = tabRow.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
        tabs[i].setBounds (tabRow.removeFromLeft (tw).reduced (2, 0));
}

// ---------------------------------------------------------------------------
GKDistEditor::GKDistEditor (GKDistProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    applyTheme (themes->get());
    buildContent();
    themes->addChangeListener (this);

    addMouseListener (this, true);
    tooltip = std::make_unique<juce::TooltipWindow> (this, 700);

    // Read the stored size first: setResizeLimits() resizes the editor, and
    // resized() writes the current size back into the UI state.
    const int w = juce::jlimit (kMinWidth, 2560, (int) proc.uiState.getProperty ("width", 1280));
    const int h = juce::jlimit (kMinHeight, 1600, (int) proc.uiState.getProperty ("height", 900));
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 2560, 1600);
    setSize (w, h);
    setWantsKeyboardFocus (true);

    proc.commitUndoPoint();   // undo baseline
    setGpu ((bool) proc.uiState.getProperty ("gpu", true));
}

GKDistEditor::~GKDistEditor()
{
    themes->removeChangeListener (this);
    removeMouseListener (this);
    proc.clearAllSolos();
    if (gl.isAttached()) gl.detach();
    tooltip.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void GKDistEditor::buildContent()
{
    // A fresh LookAndFeel and fresh components: colours set at construction
    // time (LookAndFeel, button "on" colours, accents) follow the new theme.
    const int selected = content != nullptr ? content->display.getSelectedBand() : -1;
    content.reset();
    setLookAndFeel (nullptr);
    lnf = std::make_unique<ui::LookAndFeel>();
    content = std::make_unique<EditorContent> (proc);
    addAndMakeVisible (*content);
    setLookAndFeel (lnf.get());          // after adding the content so all of it receives the change
    content->sendLookAndFeelChange();

    content->onEdit = [this] { commit(); };
    content->onGpuToggled = [this] { setGpu ((bool) proc.uiState.getProperty ("gpu", true)); };
    content->onResizeRequest = [this] (int w, int h) { setSize (w, h); };
    content->onThemeRequest = [this] (ThemeId t) { setTheme (t); };
    content->setBounds (getLocalBounds());
    if (selected >= 0) content->display.selectBand (selected);
}

void GKDistEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Rebuild after the menu callback that triggered the change has returned.
    juce::Component::SafePointer<GKDistEditor> safe (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe == nullptr) return;
        safe->buildContent();
        safe->repaint();
    });
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
    if (content != nullptr) content->setBounds (getLocalBounds());
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
