// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../Params.h"
#include <cmath>
#include <cstdint>

namespace gkd::dsp
{

/** Modulation sources (two LFOs, input and side-chain envelopes) and the
    8-slot matrix that routes them to band parameters. Runs at host rate,
    one value per control block; the bands ramp their parameters in between. */
class ModMatrix
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        for (int l = 0; l < 2; ++l)
        {
            phase[l] = 0.0;
            rndPrev[l] = rndNext[l] = rndHold[l] = 0.0;
            value[l] = 0.0;
        }
        envIn = envSc = 0.0;
        samplesSinceTransport = 0;
        for (auto& v : slotValue) v = 0.0;
    }

    /** Host transport at the start of the host block (call before processing it). */
    void setTransport (bool isPlaying, double bpmNow, double ppqAtBlockStart) noexcept
    {
        playing = isPlaying && bpmNow > 1.0 && std::isfinite (ppqAtBlockStart);
        bpm = std::isfinite (bpmNow) && bpmNow > 1.0 ? bpmNow : 120.0;
        ppq0 = std::isfinite (ppqAtBlockStart) ? ppqAtBlockStart : 0.0;
        samplesSinceTransport = 0;
    }

    /** Advances all sources by n host samples and computes every band's offsets.
        inL/inR: main input, scL/scR: side-chain (may be null). */
    void advance (int n, const float* inL, const float* inR, const float* scL, const float* scR,
                  const GlobalParams& g, const ModSlot* slots, BandMod* out) noexcept
    {
        if (n <= 0) return;

        // --- LFOs ---------------------------------------------------------------
        for (int l = 0; l < 2; ++l)
        {
            if (g.lfoSync[l] && playing)
            {
                const double ppq = ppq0 + (double) (samplesSinceTransport + n) * bpm / (60.0 * fs);
                const double cyc = ppq / lfoDivBeats (g.lfoDiv[l]);
                const double ph = cyc - std::floor (cyc);
                if (ph < phase[l]) newRandom (l);   // wrapped
                phase[l] = ph;
            }
            else
            {
                const double rate = g.lfoSync[l] ? bpm / 60.0 / lfoDivBeats (g.lfoDiv[l]) : g.lfoRate[l];
                phase[l] += rate * (double) n / fs;
                if (phase[l] >= 1.0)
                {
                    phase[l] -= std::floor (phase[l]);
                    newRandom (l);
                }
            }
            value[l] = shape (g.lfoShape[l], l, phase[l]);
        }
        samplesSinceTransport += n;

        // --- Envelopes (peak, attack / release) ----------------------------------
        const double aAtk = 1.0 - std::exp (-1.0 / (std::max (0.1, g.envAttack) * 0.001 * fs));
        const double aRel = 1.0 - std::exp (-1.0 / (std::max (1.0, g.envRelease) * 0.001 * fs));
        auto follow = [&] (double& env, const float* l, const float* r)
        {
            if (l == nullptr) { env *= std::pow (1.0 - aRel, (double) n); return; }   // no signal: release
            for (int i = 0; i < n; ++i)
            {
                const double x = std::max (std::abs ((double) l[i]), r != nullptr ? std::abs ((double) r[i]) : 0.0);
                const double v = std::isfinite (x) ? std::min (x, 16.0) : 0.0;
                env += (v > env ? aAtk : aRel) * (v - env);
            }
        };
        follow (envIn, inL, inR);
        follow (envSc, scL, scR);

        // --- Matrix ----------------------------------------------------------------
        for (int b = 0; b < kNumBands; ++b) out[b] = BandMod();
        for (int s = 0; s < kNumModSlots; ++s)
        {
            const auto& slot = slots[s];
            const double src = sourceValue (slot.source);
            slotValue[s] = src * slot.amount;
            if (slot.source == ModSource::Off || slot.amount == 0.0) continue;
            if (slot.band < 0)
                for (int b = 0; b < kNumBands; ++b) out[b].add (slot.target, slotValue[s]);
            else if (slot.band < kNumBands)
                out[slot.band].add (slot.target, slotValue[s]);
        }
    }

    /** Unipolar envelope value 0..1 from a level (-60 dBFS .. 0 dBFS). */
    static double envToUnit (double lin) noexcept
    {
        const double db = 20.0 * std::log10 (std::max (lin, 1.0e-9));
        return std::clamp ((db + 60.0) / 60.0, 0.0, 1.0);
    }

    double sourceValue (ModSource s) const noexcept
    {
        switch (s)
        {
            case ModSource::Lfo1:         return value[0];
            case ModSource::Lfo2:         return value[1];
            case ModSource::InputEnv:     return envToUnit (envIn);
            case ModSource::SidechainEnv: return envToUnit (envSc);
            case ModSource::Off:
            case ModSource::Count:
            default:                      return 0.0;
        }
    }

    double lfoValue (int l) const noexcept { return value[l & 1]; }
    double lfoPhase (int l) const noexcept { return phase[l & 1]; }
    double slotOutput (int s) const noexcept { return slotValue[juce::jlimit (0, kNumModSlots - 1, s)]; }

private:
    void newRandom (int l) noexcept
    {
        rndPrev[l] = rndNext[l];
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        rndNext[l] = ((rng >> 8) * (1.0 / 16777216.0)) * 2.0 - 1.0;
        rndHold[l] = rndNext[l];
    }

    double shape (LfoShape s, int l, double ph) const noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        switch (s)
        {
            case LfoShape::Sine:         return std::sin (twoPi * ph);
            case LfoShape::Triangle:     return ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
            case LfoShape::SawUp:        return 2.0 * ph - 1.0;
            case LfoShape::SawDown:      return 1.0 - 2.0 * ph;
            case LfoShape::Square:       return ph < 0.5 ? 1.0 : -1.0;
            case LfoShape::SampleHold:   return rndHold[l];
            case LfoShape::SmoothRandom:
            {
                const double t = 0.5 - 0.5 * std::cos (3.141592653589793 * ph);
                return rndPrev[l] + t * (rndNext[l] - rndPrev[l]);
            }
            case LfoShape::Count:
            default:                     return 0.0;
        }
    }

    double fs = 48000.0;
    bool playing = false;
    double bpm = 120.0, ppq0 = 0.0;
    long long samplesSinceTransport = 0;
    double phase[2] {}, value[2] {};
    double rndPrev[2] {}, rndNext[2] {}, rndHold[2] {};
    uint32_t rng = 0x2545f491u;
    double envIn = 0.0, envSc = 0.0;
    double slotValue[kNumModSlots] {};
};

} // namespace gkd::dsp
