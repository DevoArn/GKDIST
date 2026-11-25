// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "FilterDesign.h"
#include <cstring>

namespace gkd::dsp
{

/** Cascade of biquads (TDF-II, double) for up to two channels. New
    coefficients are reached by per-sample linear interpolation over one
    processing block, so frequency / Q changes never click. */
struct FilterChain
{
    int    num = 0;
    Biquad cur[kMaxSections];
    Biquad tgt[kMaxSections];
    double gain = 1.0, gainTgt = 1.0;
    double z[kMaxSections][2][2] {};
    bool   moving = false;

    void resetState() noexcept { std::memset (z, 0, sizeof (z)); }

    void clear() noexcept
    {
        num = 0;
        gain = gainTgt = 1.0;
        moving = false;
        resetState();
    }

    bool isIdentity() const noexcept { return num == 0 && gain == 1.0 && gainTgt == 1.0; }

    void setTarget (const DigitalFilter& d, bool snap) noexcept
    {
        if (d.num != num)
        {
            for (int s = num; s < d.num; ++s)
                z[s][0][0] = z[s][0][1] = z[s][1][0] = z[s][1][1] = 0.0;
            snap = true;
        }
        num = d.num;
        for (int s = 0; s < num; ++s)
            tgt[s] = d.sec[s];
        gainTgt = d.gain;
        if (snap)
        {
            for (int s = 0; s < num; ++s)
                cur[s] = tgt[s];
            gain = gainTgt;
            moving = false;
        }
        else
        {
            moving = true;
        }
    }

    void process (int ch, double* x, int n) noexcept
    {
        const double invN = 1.0 / (double) n;
        for (int s = 0; s < num; ++s)
        {
            double b0 = cur[s].b0, b1 = cur[s].b1, b2 = cur[s].b2, a1 = cur[s].a1, a2 = cur[s].a2;
            double* zz = z[s][ch];
            double z1 = zz[0], z2 = zz[1];
            if (moving)
            {
                const double db0 = (tgt[s].b0 - b0) * invN, db1 = (tgt[s].b1 - b1) * invN, db2 = (tgt[s].b2 - b2) * invN;
                const double da1 = (tgt[s].a1 - a1) * invN, da2 = (tgt[s].a2 - a2) * invN;
                for (int i = 0; i < n; ++i)
                {
                    b0 += db0; b1 += db1; b2 += db2; a1 += da1; a2 += da2;
                    const double xi = x[i];
                    const double y = b0 * xi + z1;
                    z1 = b1 * xi - a1 * y + z2;
                    z2 = b2 * xi - a2 * y;
                    x[i] = y;
                }
            }
            else
            {
                for (int i = 0; i < n; ++i)
                {
                    const double xi = x[i];
                    const double y = b0 * xi + z1;
                    z1 = b1 * xi - a1 * y + z2;
                    z2 = b2 * xi - a2 * y;
                    x[i] = y;
                }
            }
            // A filter driven into overflow (NaN / inf) restarts silently.
            if (! (std::isfinite (z1) && std::isfinite (z2))) { z1 = z2 = 0.0; }
            zz[0] = z1;
            zz[1] = z2;
        }

        if (moving && gain != gainTgt)
        {
            double g = gain;
            const double dg = (gainTgt - gain) * invN;
            for (int i = 0; i < n; ++i) { g += dg; x[i] *= g; }
        }
        else if (gain != 1.0)
        {
            for (int i = 0; i < n; ++i) x[i] *= gain;
        }
    }

    void commit() noexcept
    {
        if (! moving) return;
        for (int s = 0; s < num; ++s)
            cur[s] = tgt[s];
        gain = gainTgt;
        moving = false;
    }
};

} // namespace gkd::dsp
