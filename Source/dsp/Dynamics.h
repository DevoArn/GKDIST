// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterDesign.h"

namespace gkd::dsp
{

/** Band-limited, stereo-linked level detector for a dynamic band. */
class EnvelopeDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0.0;
        env = 0.0;
    }

    void setFilter (const DigitalFilter& f) noexcept
    {
        hasFilter = f.num > 0;
        if (hasFilter) c = f.sec[0];
        g = f.gain;
    }

    void setTimes (double attackMs, double releaseMs) noexcept
    {
        atk = 1.0 - std::exp (-1.0 / (std::max (0.05, attackMs) * 0.001 * fs));
        rel = 1.0 - std::exp (-1.0 / (std::max (1.0, releaseMs) * 0.001 * fs));
    }

    /** l / r: detection signal (r may be nullptr for mono). */
    void process (const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double a = filt (0, sanitise (l[i]));
            const double b = r != nullptr ? filt (1, sanitise (r[i])) : a;
            const double pw = std::max (a * a, b * b);
            env += (pw > env ? atk : rel) * (pw - env);
        }
        if (! std::isfinite (env) || env < 1.0e-30) reset();
    }

    double levelDb() const noexcept { return 10.0 * std::log10 (env + 1.0e-24); }

private:
    static double sanitise (float x) noexcept
    {
        return std::isfinite (x) ? (double) juce::jlimit (-64.0f, 64.0f, x) : 0.0;
    }

    double filt (int ch, double x) noexcept
    {
        if (! hasFilter) return x * g;
        double* s = z[ch];
        const double y = c.b0 * x + s[0];
        s[0] = c.b1 * x - c.a1 * y + s[1];
        s[1] = c.b2 * x - c.a2 * y;
        return y * g;
    }

    double fs = 48000.0;
    Biquad c;
    double g = 1.0;
    bool hasFilter = false;
    double z[2][2] {};
    double env = 0.0;
    double atk = 0.1, rel = 0.01;
};

/** Dynamics amount 0..1: 0 below the threshold, rising over a soft knee to 1
    when the level is 12 dB above the threshold. The band's gain, drive and
    wet each move by amount * their own range. */
inline double dynamicAmount (double levelDb, double thresholdDb) noexcept
{
    constexpr double knee = 6.0, span = 12.0;
    const double over = levelDb - thresholdDb;
    double o;
    if (over <= -0.5 * knee)      o = 0.0;
    else if (over < 0.5 * knee)   o = (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
    else                          o = over;
    return juce::jlimit (0.0, 1.0, o / span);
}

} // namespace gkd::dsp
