// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "SnapshotBuffer.h"
#include "dsp/DistEngine.h"

namespace gkd
{

class GKDistProcessor : public juce::AudioProcessor
{
public:
    GKDistProcessor();
    ~GKDistProcessor() override;

    // --- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // --- Shared state -------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    ParamRefs refs;
    juce::ValueTree uiState { "UI" };
    juce::String currentPresetName { "Default" };
    SnapshotBuffer preTap, postTap;

    double getCurrentSampleRate() const noexcept { return currentRate.load(); }
    float  getDynAmount (int band) const noexcept  { return dynAmount[(size_t) band].load (std::memory_order_relaxed); }
    float  getCompGr (int band) const noexcept     { return compGr[(size_t) band].load (std::memory_order_relaxed); }
    float  getGlobalCompGr() const noexcept        { return globalCompGr.load(); }
    float  getLimiterGr() const noexcept           { return limiterGr.load(); }
    float  getOutputPeakDb() const noexcept        { return outPeakDb.load(); }
    int    getActiveLatency() const noexcept       { return activeLatency.load(); }
    bool   isSidechainActive() const noexcept      { return sidechainActive.load(); }

    // --- Editing helpers (message thread) ---------------------------------------
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    void  setParam (const juce::String& id, float value);   // wrapped in a host gesture
    float getParam (const juce::String& id) const;
    int   findFreeBand() const;
    int   createBand (Shape shape, double freq, double driveDb, double q, DistType type);
    void  deleteBand (int band);
    void  resetBand (int band);
    void  clearAllSolos();

    void commitUndoPoint();
    void resetUndoBaseline();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    int  getABSlot() const noexcept { return abSlot; }
    void switchAB();
    void copyToOtherSlot();

    void loadParameterState (const juce::ValueTree& params, bool recordUndo);
    juce::ValueTree getDefaultParameterState() const { return defaultState.createCopy(); }

private:
    void initUIDefaults();
    void activateEngine (int osLog2);

    dsp::DistEngine engines[4];
    int activeOs = 2;
    bool prepared = false;
    int engineBlock = 512, numChannels = 2;

    BandParams bandsRT[kNumBands];
    float dynRT[kNumBands] {}, grRT[kNumBands] {};
    std::array<std::atomic<float>, kNumBands> dynAmount {}, compGr {};

    enum class Switch { Idle, FadingOut, FadingIn };
    Switch switchState = Switch::Idle;
    float switchGain = 1.0f, switchStep = 1.0f / 256.0f;

    juce::AudioBuffer<float> dryDelay, dryScratch;
    int dryWrite = 0;
    float bypassMix = 0.0f, bypassStep = 1.0f / 512.0f;

    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool>   sidechainActive { false };
    std::atomic<int>    activeLatency { 0 };
    std::atomic<float>  globalCompGr { 0.0f }, limiterGr { 0.0f }, outPeakDb { -100.0f };

    std::vector<juce::ValueTree> undoStack, redoStack;
    juce::ValueTree undoBaseline;
    juce::ValueTree abSlots[2];
    int abSlot = 0;
    juce::ValueTree defaultState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GKDistProcessor)
};

} // namespace gkd
