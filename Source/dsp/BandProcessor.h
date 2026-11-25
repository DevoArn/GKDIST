// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterChain.h"
#include "Dynamics.h"
#include "Modules.h"

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

/** One band: takes its region of the signal (bell / low / high / full),
    processes it (filter, distortion, phat, noise, compressor, clipper) and
    puts the result back:  y = x + fade * (gain * (b + wet * (s - b)) - b).
    With wet = 0 and gain = 0 dB, or fade = 0, the band is exactly transparent. */
class BandProcessor
{
public:
    void prepare (double hostRate, int osFactor, int bandIndex);
    void reset();

    /** Control update for one block of hostCount host-rate samples.
        detL / detR: host-rate detection signal (input or side-chain). */
    void update (const BandParams& p, int hostCount, const float* detL, const float* detR);

    bool audible() const noexcept { return fade > 0.0 || fadeTarget > 0.0; }

    /** Processes n samples at the processing (oversampled) rate in place.
        soloL / soloR (optional) receive the band output for soloing. */
    void process (double* L, double* R, int n, bool stereo, BandScratch& s, double* soloL, double* soloR) noexcept;

    float dynAmount() const noexcept { return (float) amount; }
    float compGainReduction() const noexcept { return keyCompOn ? (float) comp.gainReductionDb() : 0.0f; }

private:
    struct Key
    {
        bool active = false;
        int shape = 0, slope = 0, type = 0, ftype = 0, fpost = 0, ntype = 0, comp = 0, clip = 0;
        bool operator== (const Key& o) const noexcept
        {
            if (! active && ! o.active) return true;
            return active == o.active && shape == o.shape && slope == o.slope && type == o.type
                && ftype == o.ftype && fpost == o.fpost && ntype == o.ntype && comp == o.comp && clip == o.clip;
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
    bool keyCompOn = false;
    BandParams cur;                // latest parameters (continuous values smoothed below)

    // Smoothed controls
    double sLogF = 0, sLogQ = 0, sDrive = 0, sBias = 0, sWet = 1, sGain = 0, sLogFF = 0, sLogRes = 0;
    double sPhat = 0, sNoiseDb = kNoiseOffDb, sFollow = 0, sCeil = 0;
    double amount = 0.0;

    // Per-block targets and their values at the start of the block
    double rDrive = 1, rBias = 0, rAuto = 1, rWet = 1, rGain = 1, rPhat = 0, rNoise = 0;
    double tDrive = 1, tBias = 0, tAuto = 1, tWet = 1, tGain = 1, tPhat = 0, tNoise = 0;
    double fade = 0.0, fadeTarget = 0.0, fadeStep = 0.001;
    double crushBitsNow = 16.0;
    int crushHoldNow = 1;

    double regionKey[4] { -1, -1, -1, -1 }, filterKey[3] { -1, -1, -1 }, autoKey[3] { -1e9, -1e9, -1 };
    double autoFactor = 1.0;
    double detKey[3] { -1, -1, -1 };

    FilterChain region, noiseRegion, filt;
    DCBlocker dc;
    Phat phat;
    NoiseGen noise[2];
    Compressor comp;
    Clipper clipper;
    EnvelopeDetector det;
    double noiseEnv = 0.0, noiseEnvRel = 0.999;
    int crushCount = 0;
    double crushHeld[2] {};
};

} // namespace gkd::dsp
