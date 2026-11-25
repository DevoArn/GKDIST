// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Shapers.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gkd::dsp
{

inline double dbToLin (double db) noexcept { return std::pow (10.0, db * 0.05); }
inline double linToDb (double g) noexcept  { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

// ---------------------------------------------------------------------------
/** One-pole DC blocker (5 Hz). */
struct DCBlocker
{
    double r = 0.9999;
    double x1[2] {}, y1[2] {};

    void prepare (double fs) noexcept { r = std::exp (-2.0 * 3.14159265358979323846 * 5.0 / fs); reset(); }
    void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0; }

    void process (int ch, double* x, int n) noexcept
    {
        double px = x1[ch], py = y1[ch];
        for (int i = 0; i < n; ++i)
        {
            const double y = x[i] - px + r * py;
            px = x[i];
            py = y;
            x[i] = y;
        }
        x1[ch] = px;
        y1[ch] = std::isfinite (py) ? py : 0.0;
    }
};

// ---------------------------------------------------------------------------
/** Feed-forward peak compressor, stereo-linked, soft knee (6 dB). The gain
    is computed every kStep samples and interpolated in between. */
class Compressor
{
public:
    static constexpr int kStep = 8;

    void prepare (double sampleRate) noexcept { fs = sampleRate; reset(); }

    void reset() noexcept
    {
        grDb = 0.0;
        peak = 0.0;
        count = 0;
        curGain = dbToLin (makeupDb);
        stepGain = 0.0;
    }

    void set (double thresholdDb, double ratio, double attackMs, double releaseMs, double makeup) noexcept
    {
        thr = thresholdDb;
        slope = 1.0 - 1.0 / std::max (1.0, ratio);
        aAtk = 1.0 - std::exp (-(double) kStep / (std::max (0.05, attackMs) * 0.001 * fs));
        aRel = 1.0 - std::exp (-(double) kStep / (std::max (1.0, releaseMs) * 0.001 * fs));
        makeupDb = makeup;
    }

    /** Static gain reduction (dB, >= 0) for a level. */
    double computeGr (double levelDb) const noexcept
    {
        constexpr double knee = 6.0;
        const double over = levelDb - thr;
        if (2.0 * over < -knee) return 0.0;
        if (2.0 * std::abs (over) <= knee) return slope * (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
        return slope * over;
    }

    void process (double* L, double* R, int n, bool stereo) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double a = std::abs (L[i]);
            const double b = stereo ? std::abs (R[i]) : a;
            peak = std::max (peak, std::max (a, b));
            if (++count >= kStep)
            {
                count = 0;
                const double target = computeGr (linToDb (peak));
                peak = 0.0;
                grDb += (target > grDb ? aAtk : aRel) * (target - grDb);
                if (! std::isfinite (grDb)) grDb = 0.0;
                const double g = dbToLin (makeupDb - grDb);
                stepGain = (g - curGain) / (double) kStep;
            }
            curGain += stepGain;
            L[i] *= curGain;
            if (stereo) R[i] *= curGain;
        }
    }

    double gainReductionDb() const noexcept { return grDb; }

private:
    double fs = 48000.0;
    double thr = -18.0, slope = 0.75, aAtk = 0.1, aRel = 0.01, makeupDb = 0.0;
    double grDb = 0.0, peak = 0.0, curGain = 1.0, stepGain = 0.0;
    int count = 0;
};

// ---------------------------------------------------------------------------
/** Clip / limit stage with a ceiling. Soft = tanh knee, Hard = clamp,
    Limit = instant-attack peak limiter (plus a clamp as a safety net). */
class Clipper
{
public:
    void prepare (double sampleRate) noexcept
    {
        rel = std::exp (-1.0 / (0.05 * sampleRate));   // 50 ms release
        reset();
    }

    void reset() noexcept { env = 0.0; }

    void process (ClipMode mode, double ceilingDb, double* L, double* R, int n, bool stereo) noexcept
    {
        const double c = dbToLin (ceilingDb);
        switch (mode)
        {
            case ClipMode::Soft:
            {
                const double inv = 1.0 / c;
                for (int i = 0; i < n; ++i)
                {
                    L[i] = c * fastTanh (L[i] * inv);
                    if (stereo) R[i] = c * fastTanh (R[i] * inv);
                }
                break;
            }
            case ClipMode::Hard:
                for (int i = 0; i < n; ++i)
                {
                    L[i] = std::clamp (L[i], -c, c);
                    if (stereo) R[i] = std::clamp (R[i], -c, c);
                }
                break;
            case ClipMode::Limit:
                for (int i = 0; i < n; ++i)
                {
                    const double pk = std::max (std::abs (L[i]), stereo ? std::abs (R[i]) : 0.0);
                    env = pk > env ? pk : pk + rel * (env - pk);
                    const double g = env > c ? c / env : 1.0;
                    L[i] = std::clamp (L[i] * g, -c, c);
                    if (stereo) R[i] = std::clamp (R[i] * g, -c, c);
                }
                if (! std::isfinite (env)) env = 0.0;
                break;
            case ClipMode::Off:
            case ClipMode::Count:
            default:
                break;
        }
    }

private:
    double env = 0.0, rel = 0.999;
};

// ---------------------------------------------------------------------------
/** Noise source with unit RMS: white, pink or vinyl-like crackle. */
class NoiseGen
{
public:
    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        fs = sampleRate;
        state = seed != 0 ? seed : 0x9e3779b9u;
        crackleDecay = std::exp (-1.0 / (0.0004 * fs));
        crackleTone = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 3000.0 / fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& v : pink) v = 0.0;
        crackleEnv = crackleLp = 0.0;
    }

    double next (NoiseType t) noexcept
    {
        const double w = white();
        switch (t)
        {
            case NoiseType::Pink:
            {
                // Paul Kellet's refined pink filter
                pink[0] = 0.99886 * pink[0] + w * 0.0555179;
                pink[1] = 0.99332 * pink[1] + w * 0.0750759;
                pink[2] = 0.96900 * pink[2] + w * 0.1538520;
                pink[3] = 0.86650 * pink[3] + w * 0.3104856;
                pink[4] = 0.55000 * pink[4] + w * 0.5329522;
                pink[5] = -0.7616 * pink[5] - w * 0.0168980;
                const double p = pink[0] + pink[1] + pink[2] + pink[3] + pink[4] + pink[5] + pink[6] + w * 0.5362;
                pink[6] = w * 0.115926;
                return p * kPinkScale;
            }
            case NoiseType::Crackle:
            {
                // Sparse random clicks (about 40 per second) with a short decay.
                if (uniform() < 40.0 / fs)
                {
                    const double amp = std::pow (uniform(), 3.0);
                    crackleEnv += (uniform() < 0.5 ? -amp : amp);
                }
                crackleEnv *= crackleDecay;
                crackleLp += crackleTone * (crackleEnv - crackleLp);
                return crackleLp * kCrackleScale * std::sqrt (fs / 48000.0);
            }
            case NoiseType::White:
            case NoiseType::Count:
            default:
                return w;
        }
    }

    // Calibrated so every type has (about) unit RMS; verified by the test-suite.
    static constexpr double kPinkScale = 0.3297;     // 0.23 measured -3.13 dB
    static constexpr double kCrackleScale = 33.9;    // 14.0 measured -7.69 dB

private:
    double uniform() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (state >> 8) * (1.0 / 16777216.0);   // [0, 1)
    }

    double white() noexcept { return (uniform() * 2.0 - 1.0) * 1.7320508075688772; }   // unit RMS

    double fs = 48000.0;
    uint32_t state = 0x9e3779b9u;
    double pink[7] {};
    double crackleEnv = 0.0, crackleLp = 0.0, crackleDecay = 0.99, crackleTone = 0.3;
};

// ---------------------------------------------------------------------------
/** "Phat": low-end weight. A sub-octave generated from the low part of the
    signal (flip-flop on its zero crossings, following its envelope) plus a
    saturated copy of the lows. process() returns the signal to add. */
class Phat
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double pi = 3.14159265358979323846;
        aDet = 1.0 - std::exp (-2.0 * pi * 220.0 / sampleRate);
        aSub = 1.0 - std::exp (-2.0 * pi * 90.0 / sampleRate);
        rel  = std::exp (-1.0 / (0.06 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            lp1[c] = lp2[c] = env[c] = s1[c] = s2[c] = 0.0;
            state[c] = 1;
            flip[c] = 1.0;
        }
    }

    double process (int c, double x) noexcept
    {
        lp1[c] += aDet * (x - lp1[c]);
        lp2[c] += aDet * (lp1[c] - lp2[c]);
        const double low = lp2[c];
        const double a = std::abs (low);
        env[c] = a > env[c] ? a : a + rel * (env[c] - a);

        // Zero-crossing flip-flop with hysteresis -> octave below the lows.
        const double hyst = 0.05 * env[c] + 1.0e-6;
        if (state[c] > 0 && low < -hyst) state[c] = -1;
        else if (state[c] < 0 && low > hyst) { state[c] = 1; flip[c] = -flip[c]; }

        const double sq = flip[c] * env[c];
        s1[c] += aSub * (sq - s1[c]);
        s2[c] += aSub * (s1[c] - s2[c]);

        // Sub-octave + saturation harmonics of the lows + a little extra low end.
        const double warm = 0.5 * fastTanh (2.0 * low) - 0.5 * low;
        const double out = 1.6 * s2[c] + 1.2 * warm + 0.5 * low;
        return std::isfinite (out) ? out : 0.0;
    }

private:
    double aDet = 0.01, aSub = 0.005, rel = 0.999;
    double lp1[2] {}, lp2[2] {}, env[2] {}, s1[2] {}, s2[2] {};
    int state[2] { 1, 1 };
    double flip[2] { 1.0, 1.0 };
};

// ---------------------------------------------------------------------------
/** Brick-wall output limiter with look-ahead (host rate). The gain follows
    the minimum required gain over the look-ahead window; a final clamp
    guarantees that no sample exceeds the ceiling. */
class LookaheadLimiter
{
public:
    void prepare (double sampleRate, int numChannels, int lookaheadSamples)
    {
        fs = sampleRate;
        channels = std::max (1, std::min (2, numChannels));
        look = std::max (1, lookaheadSamples);
        for (auto& d : delay) d.assign ((size_t) look, 0.0f);
        req.assign ((size_t) look + 1, 1.0);
        aAtk = 1.0 - std::exp (-5.0 / look);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (req.begin(), req.end(), 1.0);
        pos = rpos = 0;
        g = 1.0;
    }

    int latencySamples() const noexcept { return look; }
    double gainReductionDb() const noexcept { return -linToDb (g); }

    void process (float* const* io, int numChannels, int n, bool enabled, double ceilingDb, double releaseMs) noexcept
    {
        const int nch = std::min (numChannels, channels);
        const double c = dbToLin (ceilingDb);
        const double aRel = 1.0 - std::exp (-1.0 / (std::max (1.0, releaseMs) * 0.001 * fs));
        for (int i = 0; i < n; ++i)
        {
            double pk = 0.0;
            for (int ch = 0; ch < nch; ++ch) pk = std::max (pk, (double) std::abs (io[ch][i]));
            req[(size_t) rpos] = (enabled && pk > c) ? c / pk : 1.0;
            if (++rpos == (int) req.size()) rpos = 0;

            double m = 1.0;
            for (double r : req) m = std::min (m, r);
            g += (m < g ? aAtk : aRel) * (m - g);
            if (! std::isfinite (g)) g = 1.0;

            for (int ch = 0; ch < nch; ++ch)
            {
                auto& d = delay[(size_t) ch];
                const double y = (double) d[(size_t) pos];
                d[(size_t) pos] = io[ch][i];
                double out = y * g;
                if (enabled) out = std::clamp (out, -c, c);
                io[ch][i] = (float) out;
            }
            if (++pos == look) pos = 0;
        }
    }

private:
    double fs = 48000.0;
    int channels = 2, look = 48, pos = 0, rpos = 0;
    std::vector<float> delay[2];
    std::vector<double> req;
    double g = 1.0, aAtk = 0.1;
};

} // namespace gkd::dsp
