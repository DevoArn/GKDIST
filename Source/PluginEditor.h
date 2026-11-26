// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/DistDisplay.h"
#include "ui/BandPanel.h"
#include "ui/MixerPanel.h"
#include "ui/ModPanel.h"
#include "ui/GlobalPanel.h"
#include "ui/TopBar.h"
#include "ui/WaveformView.h"

namespace gkd
{

using ui::ThemeId;

/** Everything inside the editor window. Rebuilt when the colour theme changes. */
class EditorContent : public juce::Component
{
public:
    explicit EditorContent (GKDistProcessor& p);

    void paint (juce::Graphics&) override;
    void resized() override;

    void showPage (int page);
    int  getPage() const noexcept { return page; }

    std::function<void()> onEdit, onGpuToggled;
    std::function<void (int, int)> onResizeRequest;
    std::function<void (ThemeId)> onThemeRequest;

    GKDistProcessor& proc;
    ui::TopBar top;
    ui::DistDisplay display;
    ui::WaveformView wave;
    ui::BandPanel bandPanel;
    ui::MixerPanel mixerPanel;
    ui::ModPanel modPanel;
    ui::GlobalPanel globalPanel;
    juce::TextButton tabs[3] { juce::TextButton ("BAND"), juce::TextButton ("MIXER"), juce::TextButton ("MODULATION") };

private:
    int page = 0;
};

class GKDistEditor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    explicit GKDistEditor (GKDistProcessor& p);
    ~GKDistEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;

    ui::DistDisplay& getDisplay() noexcept { return content->display; }
    ui::BandPanel& getBandPanel() noexcept { return content->bandPanel; }
    ui::MixerPanel& getMixerPanel() noexcept { return content->mixerPanel; }
    ui::ModPanel& getModPanel() noexcept { return content->modPanel; }
    ui::WaveformView& getWaveform() noexcept { return content->wave; }
    /** Shows / hides the waveform view (as the WAVEFORM button). */
    void setWaveformVisible (bool on) { proc.uiState.setProperty ("wave", on, nullptr); content->resized(); }

    /** Lower area page: 0 = band, 1 = mixer, 2 = modulation. */
    void showPage (int page) { content->showPage (page); }
    int  getPage() const noexcept { return content->getPage(); }

    /** Switches the colour theme (persist = remember it as the user's choice). */
    void setTheme (ThemeId t, bool persist = true) { themes->set (t, persist); }

    static constexpr int kMinWidth = 1280, kMinHeight = 800;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void buildContent();
    void commit() { proc.commitUndoPoint(); }
    void setGpu (bool on);

    GKDistProcessor& proc;
    juce::SharedResourcePointer<ui::ThemeManager> themes;
    std::unique_ptr<ui::LookAndFeel> lnf;
    juce::OpenGLContext gl;
    std::unique_ptr<EditorContent> content;
    std::unique_ptr<juce::TooltipWindow> tooltip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GKDistEditor)
};

} // namespace gkd
