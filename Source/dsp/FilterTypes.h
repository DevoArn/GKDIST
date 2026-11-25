// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

// Filter-shape vocabulary used by FilterDesign (shared with GKEQ's design code).
namespace gkd
{

constexpr double kMinFreq  = 10.0;
constexpr double kMaxFreq  = 30000.0;
constexpr double kMinQ     = 0.025;
constexpr double kMaxQ     = 40.0;
constexpr double kDefaultQ = 0.70710678118654752440;

enum class FilterType : int
{
    Bell = 0,
    LowShelf,
    LowCut,
    HighShelf,
    HighCut,
    Notch,
    BandPass,
    TiltShelf,
    FlatTilt,
    AllPass,
    Count
};

constexpr int kNumSlopes    = 10;   // 6,12,18,24,30,36,48,72,96,Brickwall
constexpr int kBrickwallIdx = 9;

inline int slopeDb (int idx) noexcept
{
    static constexpr int s[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96, 0 };
    return s[juce::jlimit (0, kNumSlopes - 1, idx)];
}

inline int slopeOrder (int idx) noexcept
{
    if (idx >= kBrickwallIdx) return 24;
    return slopeDb (idx) / 6;
}

} // namespace gkd
