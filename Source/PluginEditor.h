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

namespace gkd
{

class GKDistEditor : public juce::AudioProcessorEditor
{
public:
    explicit GKDistEditor (GKDistProcessor& p);
    ~GKDistEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;

    ui::DistDisplay& getDisplay() noexcept { return display; }
    ui::BandPanel& getBandPanel() noexcept { return bandPanel; }
    ui::MixerPanel& getMixerPanel() noexcept { return mixerPanel; }
    ui::ModPanel& getModPanel() noexcept { return modPanel; }

    /** Lower area page: 0 = band, 1 = mixer, 2 = modulation. */
    void showPage (int page);
    int  getPage() const noexcept { return page; }

    static constexpr int kMinWidth = 1280, kMinHeight = 800;

private:
    void commit() { proc.commitUndoPoint(); }
    void setGpu (bool on);

    GKDistProcessor& proc;
    ui::LookAndFeel lnf;
    juce::OpenGLContext gl;
    ui::TopBar top;
    ui::DistDisplay display;
    ui::BandPanel bandPanel;
    ui::MixerPanel mixerPanel;
    ui::ModPanel modPanel;
    ui::GlobalPanel globalPanel;
    juce::TextButton tabs[3] { juce::TextButton ("BAND"), juce::TextButton ("MIXER"), juce::TextButton ("MODULATION") };
    int page = 0;
    std::unique_ptr<juce::TooltipWindow> tooltip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GKDistEditor)
};

} // namespace gkd
