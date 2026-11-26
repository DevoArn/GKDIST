// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace gkd
{

GKDistProcessor::GKDistProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    refs.init (apvts);
    defaultState = apvts.copyState();
    initUIDefaults();
    for (auto& v : dynAmount) v.store (0.0f);
    for (auto& v : compGr) v.store (0.0f);
    for (auto& v : lfoValue) v.store (0.0f);
    for (auto& v : lfoPhase) v.store (0.0f);
    for (auto& v : envValue) v.store (0.0f);
    for (auto& v : modSlotValue) v.store (0.0f);
    for (auto& v : ottGain) v.store (0.0f);
}

GKDistProcessor::~GKDistProcessor() = default;

void GKDistProcessor::initUIDefaults()
{
    auto setDefault = [this] (const juce::Identifier& id, const juce::var& v)
    {
        if (! uiState.hasProperty (id)) uiState.setProperty (id, v, nullptr);
    };
    setDefault ("anaPre", true);
    setDefault ("anaPost", true);
    setDefault ("anaRange", 90);
    setDefault ("anaRes", 2);
    setDefault ("anaSpeed", 2);
    setDefault ("anaTilt", 4.5);
    setDefault ("anaFreeze", false);
    setDefault ("width", 1280);
    setDefault ("height", 900);
    setDefault ("lowerTab", 0);
    setDefault ("gpu", true);

    // Values from project files arrive as strings and may be hostile: clamp them.
    auto number = [this] (const juce::Identifier& id, double def)
    {
        const auto v = uiState.getProperty (id);
        if (v.isVoid()) return def;
        const double d = v.isString() ? v.toString().getDoubleValue() : (double) v;
        return std::isfinite (d) ? d : def;
    };
    auto clampInt = [this, number] (const juce::Identifier& id, int lo, int hi, int def)
    {
        uiState.setProperty (id, (int) std::lround (juce::jlimit ((double) lo, (double) hi, number (id, def))), nullptr);
    };
    {
        const int r = (int) number ("anaRange", 90);
        uiState.setProperty ("anaRange", r <= 75 ? 60 : (r <= 105 ? 90 : 120), nullptr);
    }
    clampInt ("anaRes", 0, 3, 2);
    clampInt ("anaSpeed", 0, 4, 2);
    uiState.setProperty ("anaTilt", juce::jlimit (0.0, 6.0, number ("anaTilt", 4.5)), nullptr);
    clampInt ("width", 1280, 2560, 1280);
    clampInt ("height", 800, 1600, 900);
    clampInt ("lowerTab", 0, 2, 0);
    setDefault ("wave", false);
    uiState.setProperty ("waveSeconds", juce::jlimit (0.25, 20.0, number ("waveSeconds", 2.0)), nullptr);
    clampInt ("waveZoom", 0, 3, 0);
    clampInt ("bandModule", 0, kNumModules - 1, (int) Module::Distortion);
}

juce::AudioProcessorParameter* GKDistProcessor::getBypassParameter() const
{
    return apvts.getParameter (ID::bypass);
}

bool GKDistProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
void GKDistProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    engineBlock = juce::jlimit (64, 8192, samplesPerBlock > 0 ? samplesPerBlock : 512);
    numChannels = juce::jlimit (1, 2, getMainBusNumOutputChannels());

    int maxLatency = 0;
    for (int os = 0; os < 4; ++os)
    {
        engines[os].prepare (sampleRate, engineBlock, numChannels, os);
        maxLatency = std::max (maxLatency, engines[os].latencySamples());
    }
    dryDelay.setSize (2, maxLatency + engineBlock + 16);
    dryDelay.clear();
    dryScratch.setSize (2, engineBlock);
    dryWrite = 0;

    switchStep = (float) (1.0 / (0.004 * sampleRate));
    bypassStep = (float) (1.0 / (0.010 * sampleRate));
    modMatrix.prepare (sampleRate);
    waveFifoData.assign ((size_t) kWaveFifoSize, dsp::WaveColumn());
    waveBlock.assign ((size_t) (engineBlock / dsp::DistEngine::kWaveColumnSamples + 2), dsp::WaveColumn());
    waveFifo.reset();
    const auto g = refs.readGlobal();
    bypassMix = g.bypass ? 1.0f : 0.0f;
    activateEngine (g.osLog2);
    switchState = Switch::Idle;
    switchGain = 1.0f;
    prepared = true;
}

void GKDistProcessor::activateEngine (int osLog2)
{
    activeOs = juce::jlimit (0, 3, osLog2);
    engines[activeOs].reset();
    const int lat = engines[activeOs].latencySamples();
    activeLatency.store (lat);
    setLatencySamples (lat);
}

void GKDistProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (! prepared || n <= 0) return;

    auto mainBus = getBusBuffer (buffer, false, 0);
    const int nch = juce::jmin (2, mainBus.getNumChannels(), numChannels);

    const float* sc[2] { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto scBus = getBusBuffer (buffer, true, 1);
            scCh = juce::jmin (2, scBus.getNumChannels());
            for (int c = 0; c < scCh; ++c) sc[c] = scBus.getReadPointer (c);
        }
    {
        float mag = 0.0f;
        for (int c = 0; c < scCh; ++c)
            mag = juce::jmax (mag, juce::FloatVectorOperations::findMaximum (sc[c], n), -juce::FloatVectorOperations::findMinimum (sc[c], n));
        sidechainActive.store (scCh > 0 && mag > 1.0e-6f);
    }

    preTap.writeMix (mainBus.getReadPointer (0), nch > 1 ? mainBus.getReadPointer (1) : nullptr, n);

    for (int b = 0; b < kNumBands; ++b)
        bandsRT[b] = refs.readBand (b);
    for (int m = 0; m < kNumModSlots; ++m)
        modSlotsRT[m] = refs.readModSlot (m);
    const GlobalParams g = refs.readGlobal();

    // Host transport for tempo-synced LFOs
    {
        bool playing = false;
        double bpm = 120.0, ppq = 0.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto q = pos->getPpqPosition()) ppq = *q;
                else playing = false;
            }
        modMatrix.setTransport (playing, bpm, ppq);
    }

    if (g.osLog2 != activeOs && switchState == Switch::Idle)
        switchState = Switch::FadingOut;
    const float bypassTarget = g.bypass ? 1.0f : 0.0f;
    float peak = 0.0f;

    for (int pos = 0; pos < n; pos += engineBlock)
    {
        const int len = juce::jmin (engineBlock, n - pos);
        float* io[2] { mainBus.getWritePointer (0) + pos, nch > 1 ? mainBus.getWritePointer (1) + pos : nullptr };
        const float* scp[2] { sc[0] != nullptr ? sc[0] + pos : nullptr, sc[1] != nullptr ? sc[1] + pos : nullptr };

        // Dry path delayed by the current latency (bypass)
        {
            const int size = dryDelay.getNumSamples();
            const int lat = activeLatency.load();
            for (int ch = 0; ch < nch; ++ch)
            {
                float* d = dryDelay.getWritePointer (ch);
                float* o = dryScratch.getWritePointer (ch);
                int w = dryWrite, r = dryWrite - lat;
                while (r < 0) r += size;
                for (int i = 0; i < len; ++i)
                {
                    d[w] = io[ch][i];
                    o[i] = d[r];
                    if (++w == size) w = 0;
                    if (++r == size) r = 0;
                }
            }
            dryWrite = (dryWrite + len) % size;
        }

        dsp::EngineInput in;
        in.bands = bandsRT;
        in.global = &g;
        in.sidechain = scCh > 0 ? scp : nullptr;
        in.scChannels = scCh;
        in.dynAmountOut = dynRT;
        in.compGrOut = grRT;
        in.telemetryOut = telRT;
        in.mod = &modMatrix;
        in.modSlots = modSlotsRT;
        const bool wantWave = juce::Time::getMillisecondCounter() - waveRequestMs.load (std::memory_order_relaxed) < 500u
                              && ! waveBlock.empty();
        int waveCount = 0;
        if (wantWave)
        {
            in.waveOut = waveBlock.data();
            in.waveCapacity = (int) waveBlock.size();
            in.waveCount = &waveCount;
        }
        engines[activeOs].process (io, nch, len, in);

        for (int i = 0; i < len; ++i)
        {
            if (switchState == Switch::FadingOut)
                switchGain = juce::jmax (0.0f, switchGain - switchStep);
            else if (switchState == Switch::FadingIn)
            {
                switchGain = juce::jmin (1.0f, switchGain + switchStep);
                if (switchGain >= 1.0f) switchState = Switch::Idle;
            }
            if (bypassMix < bypassTarget)      bypassMix = juce::jmin (bypassTarget, bypassMix + bypassStep);
            else if (bypassMix > bypassTarget) bypassMix = juce::jmax (bypassTarget, bypassMix - bypassStep);

            for (int ch = 0; ch < nch; ++ch)
            {
                const float wet = io[ch][i];
                const float dry = dryScratch.getReadPointer (ch)[i];
                const float out = switchGain * (wet + bypassMix * (dry - wet));
                io[ch][i] = out;
                peak = juce::jmax (peak, std::abs (out));
            }
        }

        // Waveform columns: output range of the final signal, then hand them to the UI.
        if (waveCount > 0)
        {
            constexpr int cs = dsp::DistEngine::kWaveColumnSamples;
            for (int k = 0; k < waveCount; ++k)
            {
                float lo = 0.0f, hi = 0.0f;
                for (int ch = 0; ch < nch; ++ch)
                    for (int i = k * cs; i < juce::jmin (len, (k + 1) * cs); ++i)
                    {
                        const float v = io[ch][i];
                        lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
                    }
                waveBlock[(size_t) k].outMin = lo;
                waveBlock[(size_t) k].outMax = hi;
                if (bypassMix > 0.5f)   // bypassed: the output is the dry signal
                    for (auto& e : waveBlock[(size_t) k].effect) e = 0.0f;
            }
            int s1, n1, s2, n2;
            waveFifo.prepareToWrite (waveCount, s1, n1, s2, n2);   // drops columns if the UI is not reading
            for (int k = 0; k < n1; ++k) waveFifoData[(size_t) (s1 + k)] = waveBlock[(size_t) k];
            for (int k = 0; k < n2; ++k) waveFifoData[(size_t) (s2 + k)] = waveBlock[(size_t) (n1 + k)];
            waveFifo.finishedWrite (n1 + n2);
        }

        if (switchState == Switch::FadingOut && switchGain <= 0.0f)
        {
            activateEngine (g.osLog2);
            switchState = Switch::FadingIn;
        }
    }

    postTap.writeMix (mainBus.getReadPointer (0), nch > 1 ? mainBus.getReadPointer (1) : nullptr, n);
    lastProcessMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    outPeakDb.store ((float) dsp::linToDb (peak));
    globalCompGr.store ((float) engines[activeOs].globalCompGainReduction());
    limiterGr.store ((float) engines[activeOs].limiterGainReduction());
    for (int b = 0; b < 3; ++b) ottGain[(size_t) b].store ((float) engines[activeOs].ottBandGainDb (b), std::memory_order_relaxed);
    for (int b = 0; b < kNumBands; ++b)
    {
        dynAmount[(size_t) b].store (dynRT[b], std::memory_order_relaxed);
        compGr[(size_t) b].store (grRT[b], std::memory_order_relaxed);
        auto& t = telemetry[(size_t) b];
        const auto& r = telRT[b];
        t.driveDb.store (r.driveDb, std::memory_order_relaxed);
        t.wet.store (r.wet, std::memory_order_relaxed);
        t.gainDb.store (r.gainDb, std::memory_order_relaxed);
        t.freq.store (r.freq, std::memory_order_relaxed);
        t.q.store (r.q, std::memory_order_relaxed);
        t.levelDb.store (r.levelDb, std::memory_order_relaxed);
        t.detDb.store (r.detDb, std::memory_order_relaxed);
        t.bias.store (r.bias, std::memory_order_relaxed);
        t.filterFreq.store (r.filterFreq, std::memory_order_relaxed);
        t.filterRes.store (r.filterRes, std::memory_order_relaxed);
        t.filterGainDb.store (r.filterGainDb, std::memory_order_relaxed);
    }
    for (int l = 0; l < 2; ++l)
    {
        lfoValue[(size_t) l].store ((float) modMatrix.lfoValue (l), std::memory_order_relaxed);
        lfoPhase[(size_t) l].store ((float) modMatrix.lfoPhase (l), std::memory_order_relaxed);
    }
    envValue[0].store ((float) modMatrix.sourceValue (ModSource::InputEnv), std::memory_order_relaxed);
    envValue[1].store ((float) modMatrix.sourceValue (ModSource::SidechainEnv), std::memory_order_relaxed);
    for (int m = 0; m < kNumModSlots; ++m) modSlotValue[(size_t) m].store ((float) modMatrix.slotOutput (m), std::memory_order_relaxed);
}

int GKDistProcessor::readWaveform (dsp::WaveColumn* dest, int maxColumns) noexcept
{
    if (waveFifoData.empty() || maxColumns <= 0) return 0;
    int s1, n1, s2, n2;
    waveFifo.prepareToRead (maxColumns, s1, n1, s2, n2);
    for (int k = 0; k < n1; ++k) dest[k] = waveFifoData[(size_t) (s1 + k)];
    for (int k = 0; k < n2; ++k) dest[n1 + k] = waveFifoData[(size_t) (s2 + k)];
    waveFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

dsp::BandTelemetry GKDistProcessor::getTelemetry (int band) const noexcept
{
    const auto& t = telemetry[(size_t) juce::jlimit (0, kNumBands - 1, band)];
    dsp::BandTelemetry r;
    r.dyn = dynAmount[(size_t) juce::jlimit (0, kNumBands - 1, band)].load (std::memory_order_relaxed);
    r.compGr = compGr[(size_t) juce::jlimit (0, kNumBands - 1, band)].load (std::memory_order_relaxed);
    r.driveDb = t.driveDb.load (std::memory_order_relaxed);
    r.wet = t.wet.load (std::memory_order_relaxed);
    r.gainDb = t.gainDb.load (std::memory_order_relaxed);
    r.freq = t.freq.load (std::memory_order_relaxed);
    r.q = t.q.load (std::memory_order_relaxed);
    r.levelDb = t.levelDb.load (std::memory_order_relaxed);
    r.detDb = t.detDb.load (std::memory_order_relaxed);
    r.bias = t.bias.load (std::memory_order_relaxed);
    r.filterFreq = t.filterFreq.load (std::memory_order_relaxed);
    r.filterRes = t.filterRes.load (std::memory_order_relaxed);
    r.filterGainDb = t.filterGainDb.load (std::memory_order_relaxed);
    return r;
}

// ---------------------------------------------------------------------------
juce::AudioProcessorEditor* GKDistProcessor::createEditor()
{
    return new GKDistEditor (*this);
}

void GKDistProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("GKDIST");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("preset", currentPresetName, nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);
    root.addChild (uiState.createCopy(), -1, nullptr);
    juce::ValueTree ab ("AB");
    ab.setProperty ("slot", abSlot, nullptr);
    for (int i = 0; i < 2; ++i)
        if (abSlots[i].isValid())
        {
            juce::ValueTree s (i == 0 ? "A" : "B");
            s.addChild (abSlots[i].createCopy(), -1, nullptr);
            ab.addChild (s, -1, nullptr);
        }
    root.addChild (ab, -1, nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void GKDistProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.isValid()) return;

    if (root.hasType ("GKDIST"))
    {
        currentPresetName = root.getProperty ("preset", "Default").toString();
        auto params = root.getChildWithName (apvts.state.getType());
        if (params.isValid()) apvts.replaceState (params);
        auto ui = root.getChildWithName ("UI");
        if (ui.isValid()) uiState.copyPropertiesFrom (ui, nullptr);
        auto ab = root.getChildWithName ("AB");
        abSlot = juce::jlimit (0, 1, (int) ab.getProperty ("slot", 0));
        for (int i = 0; i < 2; ++i)
        {
            auto s = ab.getChildWithName (i == 0 ? "A" : "B");
            abSlots[i] = s.isValid() && s.getNumChildren() > 0 ? s.getChild (0).createCopy() : juce::ValueTree();
        }
    }
    else if (root.hasType (apvts.state.getType()))
    {
        apvts.replaceState (root);
    }
    initUIDefaults();
    undoStack.clear();
    redoStack.clear();
    undoBaseline = {};
    clearAllSolos();
}

// ---------------------------------------------------------------------------
void GKDistProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

float GKDistProcessor::getParam (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

int GKDistProcessor::findFreeBand() const
{
    for (int b = 0; b < kNumBands; ++b)
        if (refs.bands[(size_t) b].p[bpUsed]->load() < 0.5f) return b;
    return -1;
}

void GKDistProcessor::resetBand (int b)
{
    // Every band parameter except "used" back to its default.
    for (int i = 0; i < kNumBandParams; ++i)
    {
        if (i == bpUsed) continue;
        if (auto* p = apvts.getParameter (bandParamId (b, i)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }
}

int GKDistProcessor::createBand (Shape shape, double freq, double driveDb, double q, DistType type)
{
    const int b = findFreeBand();
    if (b < 0) return -1;
    resetBand (b);
    setParam (bandParamId (b, bpShape), (float) (int) shape);
    setParam (bandParamId (b, bpFreq), (float) freq);
    setParam (bandParamId (b, bpQ), (float) q);
    setParam (bandParamId (b, bpDrive), (float) driveDb);
    setParam (bandParamId (b, bpType), (float) (int) type);
    // The dynamics detector bell starts on the band; it can then be moved independently.
    setParam (bandParamId (b, bpDetFreq), (float) juce::jlimit (20.0, 20000.0, freq));
    setParam (bandParamId (b, bpDetQ), (float) q);
    setParam (bandParamId (b, bpUsed), 1.0f);   // last: the band appears fully configured
    return b;
}

void GKDistProcessor::deleteBand (int b)
{
    if (b < 0 || b >= kNumBands) return;
    setParam (bandParamId (b, bpUsed), 0.0f);
    setParam (bandParamId (b, bpSolo), 0.0f);
}

void GKDistProcessor::clearAllSolos()
{
    for (int b = 0; b < kNumBands; ++b)
        if (refs.bands[(size_t) b].p[bpSolo]->load() > 0.5f)
            if (auto* p = apvts.getParameter (bandParamId (b, bpSolo)))
                p->setValueNotifyingHost (0.0f);
}

void GKDistProcessor::commitUndoPoint()
{
    auto now = apvts.copyState();
    if (! undoBaseline.isValid()) { undoBaseline = now; return; }
    if (! now.isEquivalentTo (undoBaseline))
    {
        undoStack.push_back (undoBaseline);
        if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
        redoStack.clear();
        undoBaseline = now;
    }
}

void GKDistProcessor::resetUndoBaseline() { undoBaseline = apvts.copyState(); }

void GKDistProcessor::undo()
{
    commitUndoPoint();
    if (undoStack.empty()) return;
    redoStack.push_back (apvts.copyState());
    auto s = undoStack.back();
    undoStack.pop_back();
    apvts.replaceState (s.createCopy());
    undoBaseline = s;
}

void GKDistProcessor::redo()
{
    commitUndoPoint();
    if (redoStack.empty()) return;
    undoStack.push_back (apvts.copyState());
    auto s = redoStack.back();
    redoStack.pop_back();
    apvts.replaceState (s.createCopy());
    undoBaseline = s;
}

void GKDistProcessor::switchAB()
{
    commitUndoPoint();
    abSlots[abSlot] = apvts.copyState();
    abSlot ^= 1;
    if (abSlots[abSlot].isValid()) apvts.replaceState (abSlots[abSlot].createCopy());
    commitUndoPoint();
}

void GKDistProcessor::copyToOtherSlot() { abSlots[abSlot ^ 1] = apvts.copyState(); }

void GKDistProcessor::loadParameterState (const juce::ValueTree& params, bool recordUndo)
{
    if (! params.isValid()) return;
    if (recordUndo) commitUndoPoint();
    apvts.replaceState (params.createCopy());
    if (recordUndo) commitUndoPoint();
    else            resetUndoBaseline();
}

} // namespace gkd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new gkd::GKDistProcessor();
}
