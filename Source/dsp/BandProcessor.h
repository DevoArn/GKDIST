// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterChain.h"
#include "Dynamics.h"
#include "Modules.h"
#include "Effects.h"
#include "RegionDesign.h"

namespace gkd::dsp
{

/** Scratch buffers shared by all bands (bands are processed one after another). */
struct BandScratch
{
    static constexpr int kMax = 32 * 8;   // one control block at 8x oversampling
    double bL[kMax], bR[kMax];            // band region signal
    double sL[kMax], sR[kMax];            // processed band signal
    double n1[kMax], n2[kMax];            // noise / envelope helpers
};

/** Live values of a band for the UI (written by the audio thread). */
struct BandTelemetry
{
    float dyn = 0.0f;          // dynamics amount 0..1
    float compGr = 0.0f;       // compressor gain reduction, dB
    float driveDb = 0.0f;      // effective drive (static + dynamics + modulation)
    float wet = 0.0f;          // effective wet 0..1
    float gainDb = 0.0f;       // effective gain
    float freq = 1000.0f;      // effective region frequency
    float q = 1.0f;            // effective region Q
    float levelDb = -100.0f;   // band output peak (with ~300 ms decay)
    float detDb = -100.0f;     // dynamics detector level (-100 when dynamics are off)
    float bias = 0.0f;         // effective bias (with modulation)
    float filterFreq = 5000.0f, filterRes = 0.707f, filterGainDb = 0.0f;   // effective band filter
};

/** One band: takes its region of the signal (bell / low / high / full),
    runs it through its effect chain (filter, distortion, phat, noise, FX,
    compressor, clipper - in the band's own order) and puts the result back:
        y = x + fade * (pan * mute * gain * (b + wet * (s - b)) - b).
    With wet = 0, gain = 0 dB, centre pan and no mute, or fade = 0, the band
    is exactly transparent. */
class BandProcessor
{
public:
    void prepare (double hostRate, int osFactor, int bandIndex);
    void reset();

    /** Control update for one block of hostCount host-rate samples.
        mod: modulation offsets; detL / detR: host-rate detection signal. */
    void update (const BandParams& p, const BandMod& mod, int hostCount, const float* detL, const float* detR);

    bool audible() const noexcept { return fade > 0.0 || fadeTarget > 0.0; }

    /** Processes n samples at the processing (oversampled) rate in place.
        soloL / soloR (optional) receive the band output for soloing. */
    void process (double* L, double* R, int n, bool stereo, BandScratch& s, double* soloL, double* soloR) noexcept;

    float dynAmount() const noexcept { return (float) amount; }
    float compGainReduction() const noexcept { return keyCompOn ? (float) comp.gainReductionDb() : 0.0f; }
    BandTelemetry telemetry() const noexcept;
    /** RMS of this band's change to the signal since the last call (0 when silent / bypassed). */
    float takeEffectRms() noexcept
    {
        const float r = effectCount > 0 ? (float) std::sqrt (effectAcc / (double) effectCount) : 0.0f;
        effectAcc = 0.0;
        effectCount = 0;
        return std::isfinite (r) ? r : 0.0f;
    }

private:
    struct Key
    {
        bool active = false;
        int shape = 0, slope = 0, type = 0, ftype = 0, fslope = 0, ntype = 0, comp = 0, clip = 0, fx = 0, order = 0;
        bool operator== (const Key& o) const noexcept
        {
            if (! active && ! o.active) return true;
            return active == o.active && shape == o.shape && slope == o.slope && type == o.type
                && ftype == o.ftype && fslope == o.fslope && ntype == o.ntype && comp == o.comp && clip == o.clip
                && fx == o.fx && order == o.order;
        }
    };

    static Key makeKey (const BandParams& p) noexcept;
    void applyKey (const Key& k);
    void designRegion (bool snap);
    void designFilter (bool snap);

    double hostRate = 48000.0, osRate = 48000.0;
    int osFactor = 1, index = 0;
    bool initialised = false;

    Key key;
    int order[kNumModules] { 0, 1, 2, 3, 4, 5, 6 };
    bool keyCompOn = false;

    // Smoothed controls
    double sLogF = 0, sLogQ = 0, sDrive = 0, sBias = 0, sWet = 1, sGain = 0, sLogFF = 0, sLogRes = 0, sFGain = 0;
    double sPhat = 0, sNoiseDb = kNoiseOffDb, sFollow = 0, sCeil = 0, sPan = 0, sMute = 1;
    double sLogFxRate = 0, sFxDepth = 0.5, sFxFb = 0, sFxMix = 0.5;
    double amount = 0.0;

    // Effective (smoothed + modulated) values used by the designs and the FX
    double eLogF = 0, eLogQ = 0, eLogFF = 0, eLogRes = 0, eFGain = 0;
    double eFxRate = 0.5, eFxDepth = 0.5, eFxMix = 0.5;

    // Per-block targets and their values at the start of the block
    double rDrive = 1, rBias = 0, rAuto = 1, rWet = 1, rGain = 1, rPhat = 0, rNoise = 0, rPanL = 1, rPanR = 1;
    double tDrive = 1, tBias = 0, tAuto = 1, tWet = 1, tGain = 1, tPhat = 0, tNoise = 0, tPanL = 1, tPanR = 1;
    double fade = 0.0, fadeTarget = 0.0, fadeStep = 0.001;
    double rPdDepth = 0, tPdDepth = 0, rPdTone = 0.1, tPdTone = 0.1;   // phase distortion
    double crushBitsNow = 16.0;
    int crushHoldNow = 1;

    double regionKey[4] { -1, -1, -1, -1 }, filterKey[5] { -1, -1, -1, -1, -1 }, autoKey[3] { -1e9, -1e9, -1 };
    double autoFactor = 1.0;
    double detKey[4] { -1, -1, -1, -1 };

    FilterChain region, noiseRegion, filt;
    DCBlocker dc;
    Phat phat;
    NoiseGen noise[2];
    BandFx fx;
    PhaseDistorter pd;
    Compressor comp;
    Clipper clipper;
    EnvelopeDetector det;
    double noiseEnv = 0.0, noiseEnvRel = 0.999;
    int crushCount = 0;
    double crushHeld[2] {};

    // Telemetry
    double effDriveDb = 0, effWet = 0, effGainDb = 0, effBias = 0, detLevelDb = -100.0;
    double levelEnv = 0.0, levelDecay = 0.9999;
    double effectAcc = 0.0;
    int effectCount = 0;
};

} // namespace gkd::dsp
