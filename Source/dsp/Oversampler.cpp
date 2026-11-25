// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Oversampler.h"
#include <algorithm>

namespace gkd::dsp
{

namespace
{
    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double y = 0.25 * x * x;
        for (int i = 1; i < 64; ++i)
        {
            term *= y / ((double) i * (double) i);
            sum += term;
            if (term < sum * 1.0e-17) break;
        }
        return sum;
    }
}

// ---------------------------------------------------------------------------
void HalfbandStage::design (int K, double beta)
{
    k = K;
    c.assign ((size_t) (K + 1), 0.0);
    const double half = 2.0 * K + 1.0;   // centre-to-edge distance in taps
    const double i0b = besselI0 (beta);
    const double pi = 3.14159265358979323846;

    double sum = 0.0;
    for (int j = 1; j <= K + 1; ++j)
    {
        const double o = 2.0 * j - 1.0;                       // odd offset from the centre tap
        const double sinc = ((j % 2 == 1) ? 1.0 : -1.0) / (pi * o);
        const double t = o / (half + 1.0);
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / i0b;
        c[(size_t) (j - 1)] = sinc * w;
        sum += sinc * w;
    }
    // Exact unity DC gain: the side taps of a half-band filter sum to 1/4.
    for (auto& v : c) v *= 0.25 / sum;
    for (auto& v : c) v *= 2.0;   // interpolator gain of 2 folded in (c_j = 2 h_j)
}

void HalfbandStage::prepare (int numChannels)
{
    upHist.assign ((size_t) numChannels, Ring());
    dnEven.assign ((size_t) numChannels, Ring());
    dnOdd.assign ((size_t) numChannels, Ring());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        upHist[(size_t) ch].init (2 * k + 2);
        dnEven[(size_t) ch].init (k + 1);
        dnOdd[(size_t) ch].init (2 * k + 2);
    }
}

void HalfbandStage::reset()
{
    for (auto& r : upHist) r.clear();
    for (auto& r : dnEven) r.clear();
    for (auto& r : dnOdd) r.clear();
}

void HalfbandStage::up (int ch, const float* x, float* y, int n) noexcept
{
    auto& ring = upHist[(size_t) ch];
    const int K = k;
    const double* cj = c.data();
    for (int i = 0; i < n; ++i)
    {
        const double* W = ring.push ((double) x[i]);
        double v = 0.0;
        for (int j = 1; j <= K + 1; ++j)
            v += cj[j - 1] * (W[K + j] + W[K + 1 - j]);
        y[2 * i]     = (float) W[K];
        y[2 * i + 1] = (float) v;
    }
}

void HalfbandStage::down (int ch, const float* w, float* y, int n) noexcept
{
    auto& re = dnEven[(size_t) ch];
    auto& ro = dnOdd[(size_t) ch];
    const int K = k;
    const double* cj = c.data();
    for (int i = 0; i < n; ++i)
    {
        const double* E = re.push ((double) w[2 * i]);
        const double* O = ro.push ((double) w[2 * i + 1]);
        double v = 0.0;
        for (int j = 1; j <= K + 1; ++j)
            v += cj[j - 1] * (O[K + j] + O[K + 1 - j]);
        y[i] = (float) (0.5 * E[0] + 0.5 * v);
    }
}

// ---------------------------------------------------------------------------
void LinearPhaseOversampler::prepare (int numChannels, int factorLog2, int maxBlockSize)
{
    channels = std::max (1, numChannels);
    log2f = std::clamp (factorLog2, 0, kMaxLog2);

    // Stage 0 (1x <-> 2x): flat to ~20 kHz at 44.1 kHz, ~95 dB image rejection.
    // Higher stages have ever wider transition bands, so shorter filters suffice.
    const int K[kMaxLog2] = { 35, 6, 4 };
    for (int s = 0; s < kMaxLog2; ++s)
    {
        stages[s].design (K[s], 9.5);
        stages[s].prepare (channels);
    }

    const size_t maxN = (size_t) std::max (1, maxBlockSize);
    for (int s = 1; s <= kMaxLog2; ++s)
        level[s].assign (s <= log2f ? (size_t) channels : 0, std::vector<float> (maxN << s, 0.0f));

    latency = 0;
    for (int s = 0; s < kMaxLog2; ++s)
    {
        pad[s] = 0;
        padDelay[s].clear();
        if (s >= log2f) continue;
        const int roundTrip = 2 * stages[s].K() + 1;            // samples at rate 2^s
        const int unit = 1 << s;
        pad[s] = (unit - roundTrip % unit) % unit;
        latency += (roundTrip + pad[s]) / unit;
        if (pad[s] > 0)
        {
            padDelay[s].assign ((size_t) channels, PadDelay());
            for (auto& d : padDelay[s]) d.buf.assign ((size_t) pad[s], 0.0f);
        }
    }

    osPtr.assign ((size_t) channels, nullptr);
    reset();
}

void LinearPhaseOversampler::reset()
{
    for (auto& s : stages) s.reset();
    for (auto& v : padDelay)
        for (auto& d : v)
        {
            std::fill (d.buf.begin(), d.buf.end(), 0.0f);
            d.pos = 0;
        }
}

float* const* LinearPhaseOversampler::up (const float* const* in, int numChannels, int n) noexcept
{
    const int nch = std::min (numChannels, channels);
    if (log2f == 0)
    {
        for (int ch = 0; ch < nch; ++ch)
            osPtr[(size_t) ch] = const_cast<float*> (in[ch]);
        return osPtr.data();
    }

    for (int ch = 0; ch < nch; ++ch)
    {
        const float* src = in[ch];
        for (int s = 0; s < log2f; ++s)
        {
            float* dst = level[s + 1][(size_t) ch].data();
            stages[s].up (ch, src, dst, n << s);
            src = dst;
        }
        osPtr[(size_t) ch] = level[log2f][(size_t) ch].data();
    }
    return osPtr.data();
}

void LinearPhaseOversampler::down (float* const* out, int numChannels, int n) noexcept
{
    if (log2f == 0) return;   // processed in place
    const int nch = std::min (numChannels, channels);
    for (int ch = 0; ch < nch; ++ch)
    {
        for (int s = log2f - 1; s >= 0; --s)
        {
            const float* src = level[s + 1][(size_t) ch].data();
            float* dst = s == 0 ? out[ch] : level[s][(size_t) ch].data();
            stages[s].down (ch, src, dst, n << s);
            if (s > 0 && pad[s] > 0)
                padDelay[s][(size_t) ch].process (dst, n << s);
        }
    }
}

} // namespace gkd::dsp
