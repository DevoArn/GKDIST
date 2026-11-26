// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BandProcessor.h"
#include "Oversampler.h"
#include "Modulation.h"
#include "Ott.h"
#include <memory>

namespace gkd::dsp
{

/** One column of the waveform view: a control block (32 host samples). */
struct WaveColumn
{
    float inMin = 0, inMax = 0, outMin = 0, outMax = 0;
    float effect[kNumBands] {};   // RMS of each band's change to the signal
};

struct EngineInput
{
    const BandParams*   bands = nullptr;      // kNumBands entries
    const GlobalParams* global = nullptr;
    const float* const* sidechain = nullptr;  // nullptr when no side-chain is connected
    int                 scChannels = 0;
    float*              dynAmountOut = nullptr; // kNumBands
    float*              compGrOut = nullptr;    // kNumBands
    BandTelemetry*      telemetryOut = nullptr; // kNumBands
    ModMatrix*          mod = nullptr;          // modulation sources (shared by all engines)
    const ModSlot*      modSlots = nullptr;     // kNumModSlots
    WaveColumn*         waveOut = nullptr;      // optional: one column per kSub samples
    int                 waveCapacity = 0;
    int*                waveCount = nullptr;
};

/** The complete GKDIST signal path for one oversampling factor:

    input -> [oversample] -> band 1 ... band 24 (in series, each with its own effect chain) -> solo
          -> global HP/LP -> global compressor -> global noise -> global clipper
          -> [downsample] -> dry/wet mix -> output gain -> look-ahead limiter

    Latency = oversampler latency + 1 ms limiter look-ahead (constant for a
    given oversampling factor, whether the limiter is on or off). */
class DistEngine
{
public:
    void prepare (double hostRate, int maxBlockSize, int numChannels, int oversamplingLog2);
    void reset();

    int latencySamples() const noexcept { return os.latencySamples() + limiter.latencySamples(); }
    static int lookaheadFor (double hostRate) noexcept { return std::max (1, (int) std::lround (0.001 * hostRate)); }
    static constexpr int kWaveColumnSamples = 32;   // = kSub

    void process (float* const* io, int numChannels, int numSamples, const EngineInput& in);

    double globalCompGainReduction() const noexcept { return gCompOnNow ? gComp.gainReductionDb() : 0.0; }
    double limiterGainReduction() const noexcept { return limiter.gainReductionDb(); }
    /** OTT mode: current gain of band 0..2 (dB, + = upward boost, - = downward cut); 0 otherwise. */
    double ottBandGainDb (int b) const noexcept { return gCompOnNow && compModeNow == 1 ? ott.bandGainDb (b) : 0.0; }

private:
    static constexpr int kSub = 32;

    void updateGlobal (const GlobalParams& g, int hostCount);
    void designGlobalFilters (bool snap);

    double hostRate = 48000.0, osRate = 48000.0;
    int osLog2 = 0, osFactor = 1, channels = 2, maxBlock = 512;
    LinearPhaseOversampler os;
    BandProcessor bands[kNumBands];
    BandMod bandMods[kNumBands];
    BandScratch scratch;

    // processing-rate work buffers (one control block)
    double xL[BandScratch::kMax] {}, xR[BandScratch::kMax] {}, solL[BandScratch::kMax] {}, solR[BandScratch::kMax] {};
    double tmpL[BandScratch::kMax] {}, tmpR[BandScratch::kMax] {};

    // global section
    FilterChain hp, lp;
    Compressor gComp;
    OttCompressor ott;
    int compModeNow = 0;                    // mode in use; a change cross-fades through zero
    double sOttLowX = 0.0, sOttHighX = 0.0; // smoothed crossovers (log Hz)
    NoiseGen gNoise[2];
    LookaheadLimiter limiter;
    bool initialised = false, gCompOnNow = false;
    double sHp = 0, sLp = 0;
    double hpMix = 0, lpMix = 0, compMix = 0, clipMix = 0, noiseLvl = 0, soloMix = 0;
    double hpMixT = 0, lpMixT = 0, compMixT = 0, clipMixT = 0, noiseLvlT = 0;
    double clipCeil = 1.0;
    bool clipSoft = true;
    int filtSlope = 0;
    double filterKey[3] { -1, -1, -1 };
    NoiseType gNoiseType = NoiseType::White;

    // host-rate mix / output
    std::vector<float> dryRing[2];
    std::vector<float> dryBuf[2];
    int dryPos = 0;
    double mixNow = 1.0, outNow = 1.0;
};

} // namespace gkd::dsp
