// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../Params.h"
#include <cmath>

namespace gkd::dsp
{

/** tanh via a 7th-order Pade approximant (error < 2e-6 on |x| <= 4.97),
    saturating smoothly beyond. Several times faster than std::tanh. */
inline double fastTanh (double x) noexcept
{
    x = x < -4.97 ? -4.97 : (x > 4.97 ? 4.97 : x);
    const double x2 = x * x;
    return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)))
             / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0)));
}

/** Static transfer curves. Input is the driven signal u; every curve is
    finite and bounded for any finite input (outputs stay within about +-2). */
inline double shapeCurve (DistType t, double u) noexcept
{
    switch (t)
    {
        case DistType::SoftClip:
            return fastTanh (u);

        case DistType::HardClip:
            return u < -1.0 ? -1.0 : (u > 1.0 ? 1.0 : u);

        case DistType::Tube:
        {
            // Asymmetric: the squared term adds even harmonics.
            const double t1 = fastTanh (u);
            return fastTanh (u + 0.35 * t1 * t1);
        }

        case DistType::Tape:
            // Gentle algebraic saturation with a long, soft knee.
            return u / std::sqrt (1.0 + u * u);

        case DistType::Transistor:
        {
            // Cubic soft clip: harder knee than tanh, flat above |u| = 1.
            if (u <= -1.0) return -1.0;
            if (u >= 1.0)  return 1.0;
            return 1.5 * u - 0.5 * u * u * u;
        }

        case DistType::Diode:
        {
            // Asymmetric exponential: positive half clips at 1, negative at 0.5.
            if (u >= 0.0) return 1.0 - std::exp (-std::min (u, 60.0));
            return -0.5 * (1.0 - std::exp (2.0 * std::max (u, -30.0)));
        }

        case DistType::Fuzz:
        {
            // High gain with a built-in offset: buzzy, gated, very asymmetric.
            static const double offset = fastTanh (0.5);
            return fastTanh (3.0 * u + 0.5) - offset;
        }

        case DistType::Foldback:
        {
            // Triangle wavefolder: identical to u for |u| <= 1, folds above.
            const double v = std::max (-1.0e6, std::min (1.0e6, u));
            double ph = (v + 1.0) * 0.25;
            ph -= std::floor (ph);
            return 1.0 - 4.0 * std::abs (ph - 0.5);
        }

        case DistType::Rectify:
            // Full-wave rectifier (octave up), softly limited.
            return fastTanh (std::abs (u));

        case DistType::Bitcrush:          // quantised in the band processor; same +-1 domain
        case DistType::PhaseDist:         // phase-modulated in the band processor (PhaseDistorter)
        case DistType::Count:
        default:
            return u < -1.0 ? -1.0 : (u > 1.0 ? 1.0 : u);
    }
}

/** Shaper output including bias: shape(u + bias) - shape(bias), so a silent
    input stays silent for every bias setting. */
inline double shapeBiased (DistType t, double u, double bias) noexcept
{
    return shapeCurve (t, u + bias) - shapeCurve (t, bias);
}

/** Bit depth used by Bitcrush for a drive setting (0 dB -> 16 bit, 48 dB -> 2 bit). */
inline double crushBits (double driveDb) noexcept
{
    return juce::jlimit (2.0, 16.0, 16.0 - driveDb * (14.0 / 48.0));
}

/** Sample-and-hold length (host-rate samples) used by Bitcrush for a bias
    setting: bias 0 or below -> no rate reduction, +1 -> hold 32 samples. */
inline int crushHold (double bias) noexcept
{
    return 1 + (int) std::lround (std::max (0.0, bias) * 31.0);
}

inline double crushQuantise (double x, double bits) noexcept
{
    const double q = std::pow (2.0, bits - 1.0);
    const double v = std::max (-1.0, std::min (1.0, x));
    return std::round (v * q) / q;
}

/** Output level correction for a curve: the inverse of its gain at -18 dBFS
    (a typical programme level), so turning up drive changes the colour more
    than the loudness. */
inline double autoGainFactor (DistType t, double driveDb, double bias) noexcept
{
    if (t == DistType::Bitcrush || t == DistType::PhaseDist) return 1.0;   // level-neutral types
    constexpr int N = 32;
    constexpr double amp = 0.125;                       // -18 dBFS peak
    const double drive = std::pow (10.0, driveDb / 20.0);
    double sumIn = 0.0, sumOut = 0.0, mean = 0.0;
    double out[N];
    for (int i = 0; i < N; ++i)
    {
        const double x = amp * std::sin (2.0 * 3.14159265358979323846 * (i + 0.5) / N);
        out[i] = shapeBiased (t, x * drive, bias);
        mean += out[i];
        sumIn += x * x;
    }
    mean /= N;
    for (int i = 0; i < N; ++i)
        sumOut += (out[i] - mean) * (out[i] - mean);
    if (sumOut < 1.0e-20) return 1.0;
    return juce::jlimit (1.0 / 256.0, 16.0, std::sqrt (sumIn / sumOut));
}

} // namespace gkd::dsp
