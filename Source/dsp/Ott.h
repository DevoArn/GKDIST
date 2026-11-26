// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Modules.h"

namespace gkd::dsp
{

/** "OTT"-style 3-band upward + downward compressor.

    The signal is split at two crossovers (Linkwitz-Riley 24 dB/oct; the low
    band is all-pass compensated so the three bands sum flat). In each band
    loud parts are pushed down above a threshold (ratio ~67:1) and quiet parts
    pulled up below a lower threshold (ratio ~4:1). The make-up gain keeps a
    band at -24 dB unchanged, so DOWN and UP change the density, not the level
    of typical material. DEPTH mixes with the phase-matched dry signal.
    Gains are computed every kStep samples and interpolated in between. */
class OttCompressor
{
public:
    static constexpr int kStep = 8;
    static constexpr int kBands = 3;

    struct Settings
    {
        double lowX = 88.3, highX = 2500.0;   // crossovers, Hz
        double depth = 1.0, time = 1.0;       // 0..1, attack/release scale
        double up = 1.0, down = 1.0;          // 0..1
        double inDb = 0.0, outDb = 0.0;
    };

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        designed = false;
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : ch)
        {
            for (auto& z : c.lp1) z.reset();
            for (auto& z : c.hp1) z.reset();
            for (auto& z : c.lp2) z.reset();
            for (auto& z : c.hp2) z.reset();
            c.ap.reset();
        }
        // Start at the reference level, where the gain is neutral: no swell while
        // the envelopes settle after switching on.
        for (int b = 0; b < kBands; ++b)
        {
            env[b] = std::pow (10.0, kRefDb / 10.0);
            gainDb[b] = 0.0;
            curGain[b] = 1.0;
            stepGain[b] = 0.0;
        }
        count = 0;
    }

    void set (const Settings& s) noexcept
    {
        const double nyq = 0.45 * fs;
        const double lo = juce::jlimit (20.0, nyq * 0.25, s.lowX);
        const double hi = juce::jlimit (lo * 2.0, nyq, s.highX);
        if (! designed || std::abs (lo - lowX) > 1.0e-6 || std::abs (hi - highX) > 1.0e-6)
        {
            lowX = lo; highX = hi;
            const auto lp1 = butter (lowX, false), hp1 = butter (lowX, true);
            const auto lp2 = butter (highX, false), hp2 = butter (highX, true), ap = allpass (highX);
            for (auto& c : ch)
            {
                for (auto& z : c.lp1) z.c = lp1;
                for (auto& z : c.hp1) z.c = hp1;
                for (auto& z : c.lp2) z.c = lp2;
                for (auto& z : c.hp2) z.c = hp2;
                c.ap.c = ap;
            }
            designed = true;
        }
        depth = juce::jlimit (0.0, 1.0, s.depth);
        up = juce::jlimit (0.0, 1.0, s.up);
        down = juce::jlimit (0.0, 1.0, s.down);
        inGain = dbToLin (juce::jlimit (-24.0, 24.0, s.inDb));
        outGain = dbToLin (juce::jlimit (-24.0, 24.0, s.outDb));
        const double t = juce::jlimit (0.01, 10.0, s.time);
        for (int b = 0; b < kBands; ++b)
        {
            aAtk[b] = 1.0 - std::exp (-1.0 / (std::max (0.05, kAttackMs[b] * t) * 0.001 * fs));
            aRel[b] = 1.0 - std::exp (-1.0 / (std::max (1.0, kReleaseMs[b] * t) * 0.001 * fs));
        }
    }

    /** Static gain (dB) of a band for a detector level (dB). */
    double staticGainDb (int b, double levelDb) const noexcept
    {
        double g = 0.0;
        if (levelDb > kDownThr[b]) g -= down * (levelDb - kDownThr[b]) * (1.0 - 1.0 / kDownRatio);
        if (levelDb < kUpThr[b])
        {
            // no boost for silence: fade the upward gain out between -90 and -78 dB
            const double gate = juce::jlimit (0.0, 1.0, (levelDb + 90.0) / 12.0);
            g += gate * up * std::min (kMaxBoostDb, (kUpThr[b] - levelDb) * (1.0 - 1.0 / kUpRatio));
        }
        // make-up: a band at the reference level passes unchanged
        g += down * (kRefDb - kDownThr[b]) * (1.0 - 1.0 / kDownRatio);
        return g;
    }

    void process (double* L, double* R, int n, bool stereo) noexcept
    {
        const int nch = stereo ? 2 : 1;
        double* io[2] { L, R };
        for (int i = 0; i < n; ++i)
        {
            double band[2][kBands], dry[2];
            double pk[kBands] { 0.0, 0.0, 0.0 };
            for (int c = 0; c < nch; ++c)
            {
                auto& s = ch[c];
                const double x = io[c][i] * inGain;
                double lo = s.lp1[1].run (s.lp1[0].run (x));
                const double rest = s.hp1[1].run (s.hp1[0].run (x));
                const double mid = s.lp2[1].run (s.lp2[0].run (rest));
                const double hi = s.hp2[1].run (s.hp2[0].run (rest));
                lo = s.ap.run (lo);   // phase-match the low band to the high crossover
                band[c][0] = lo; band[c][1] = mid; band[c][2] = hi;
                dry[c] = lo + mid + hi;
                for (int b = 0; b < kBands; ++b) pk[b] = std::max (pk[b], band[c][b] * band[c][b]);
            }
            for (int b = 0; b < kBands; ++b)
                env[b] += (pk[b] > env[b] ? aAtk[b] : aRel[b]) * (pk[b] - env[b]);

            if (++count >= kStep)
            {
                count = 0;
                for (int b = 0; b < kBands; ++b)
                {
                    if (! std::isfinite (env[b])) env[b] = 0.0;
                    gainDb[b] = staticGainDb (b, 10.0 * std::log10 (env[b] + 1.0e-20));
                    stepGain[b] = (dbToLin (gainDb[b]) - curGain[b]) / (double) kStep;
                }
            }
            for (int b = 0; b < kBands; ++b) curGain[b] += stepGain[b];

            for (int c = 0; c < nch; ++c)
            {
                const double wet = band[c][0] * curGain[0] + band[c][1] * curGain[1] + band[c][2] * curGain[2];
                const double y = (dry[c] + depth * (wet - dry[c])) * outGain;
                io[c][i] = std::isfinite (y) ? y : 0.0;
            }
        }
    }

    /** Current gain of a band (dB): positive = boosted (upward), negative = cut (downward). */
    double bandGainDb (int b) const noexcept { return gainDb[juce::jlimit (0, kBands - 1, b)]; }

private:
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Biquad
    {
        Coef c;
        double z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0; }
        double run (double x) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            if (! std::isfinite (z1) || ! std::isfinite (z2)) { z1 = z2 = 0.0; return 0.0; }
            return y;
        }
    };
    struct Channel { Biquad lp1[2], hp1[2], lp2[2], hp2[2], ap; };

    // Butterworth (Q = 1/sqrt 2) sections; two in series = Linkwitz-Riley 24 dB/oct.
    Coef butter (double f, bool highPass) const noexcept
    {
        const double w = 2.0 * 3.141592653589793 * f / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al;
        Coef c;
        c.b0 = (highPass ? (1.0 + cw) : (1.0 - cw)) * 0.5 / a0;
        c.b1 = (highPass ? -(1.0 + cw) : (1.0 - cw)) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - al) / a0;
        return c;
    }

    // LR4 low pass + high pass = this 2nd-order all-pass (same Q).
    Coef allpass (double f) const noexcept
    {
        const double w = 2.0 * 3.141592653589793 * f / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al;
        Coef c;
        c.b0 = (1.0 - al) / a0;
        c.b1 = -2.0 * cw / a0;
        c.b2 = 1.0;   // (1 + al) / a0
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - al) / a0;
        return c;
    }

    // OTT-style band settings: low, mid, high
    static constexpr double kDownThr[kBands] { -33.8, -30.2, -35.5 };
    static constexpr double kUpThr[kBands]   { -40.8, -41.8, -40.8 };
    static constexpr double kDownRatio = 66.7, kUpRatio = 4.17, kMaxBoostDb = 36.0, kRefDb = -24.0;
    static constexpr double kAttackMs[kBands]  { 47.8, 22.4, 13.5 };
    static constexpr double kReleaseMs[kBands] { 282.0, 282.0, 132.0 };

    double fs = 48000.0, lowX = 88.3, highX = 2500.0;
    bool designed = false;
    double depth = 1.0, up = 1.0, down = 1.0, inGain = 1.0, outGain = 1.0;
    double aAtk[kBands] {}, aRel[kBands] {};
    double env[kBands] {}, gainDb[kBands] {}, curGain[kBands] { 1.0, 1.0, 1.0 }, stepGain[kBands] {};
    int count = 0;
    Channel ch[2];
};

} // namespace gkd::dsp
