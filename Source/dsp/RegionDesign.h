// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterDesign.h"
#include "../Params.h"

namespace gkd::dsp
{

/** -3 dB edges of a band around f0 with quality q (as for a 2nd-order band pass). */
inline void bandEdges (double f0, double q, double& lo, double& hi) noexcept
{
    const double a = 1.0 / (2.0 * std::max (0.05, q));
    const double r = std::sqrt (1.0 + a * a);
    lo = f0 * (r - a);
    hi = f0 * (r + a);
}

/** The filter that takes a band's region from the signal. Used by the band
    processor (audio) and the display (drawing), so both always agree.
    - Full: identity.
    - Low / High: Butterworth high cut / low cut, 12 / 24 / 48 dB/oct or brickwall (144 dB/oct).
    - Bell: band pass with Q; brickwall = a flat band between the band-pass
      edges, 96 dB/oct high pass + 96 dB/oct low pass (16 sections). */
inline AnalogFilter designRegionFilter (Shape shape, double freq, double q, int slope, double fs)
{
    AnalogFilter f;
    if (shape == Shape::Full) return f;
    const bool brick = slope >= kRegionBrickwall;
    DesignParams dp;
    dp.freq = freq;
    switch (shape)
    {
        case Shape::Bell:
            if (brick)
            {
                double lo, hi;
                bandEdges (freq, q, lo, hi);
                const double nyq = 0.49 * fs;
                DesignParams e;
                e.q = kDefaultQ;
                e.slopeIdx = 8;   // 96 dB/oct = 16th order = 8 sections
                e.type = FilterType::LowCut;
                e.freq = juce::jlimit (kMinFreq, std::min (kMaxFreq, nyq), lo);
                f = designAnalog (e, fs);
                if (hi < nyq)   // above Nyquist the low pass would do nothing
                {
                    e.type = FilterType::HighCut;
                    e.freq = juce::jlimit (kMinFreq, std::min (kMaxFreq, nyq), hi);
                    const auto lp = designAnalog (e, fs);
                    for (int i = 0; i < lp.num && f.num < kMaxSections; ++i) f.add() = lp.sec[i];
                    f.gain *= lp.gain;
                }
                return f;
            }
            dp.type = FilterType::BandPass;
            dp.q = q;
            break;
        case Shape::Low:  dp.type = FilterType::HighCut; dp.q = kDefaultQ; break;
        case Shape::High: dp.type = FilterType::LowCut;  dp.q = kDefaultQ; break;
        case Shape::Full:
        case Shape::Count:
        default: return f;
    }
    dp.slopeIdx = regionSlopeDesignIdx (slope);
    return designAnalog (dp, fs);
}

} // namespace gkd::dsp
