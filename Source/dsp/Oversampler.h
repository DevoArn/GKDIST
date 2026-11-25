// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <cmath>

namespace gkd::dsp
{

/** One 2x stage of a linear-phase half-band (Kaiser windowed-sinc) resampler.
    Up- and down-sampling are polyphase; the round trip has an integer delay
    of 2K+1 samples at the lower rate. */
class HalfbandStage
{
public:
    void design (int K, double kaiserBeta);
    void prepare (int numChannels);
    void reset();

    int K() const noexcept { return k; }

    /** x: n samples -> y: 2n samples. */
    void up (int ch, const float* x, float* y, int n) noexcept;
    /** w: 2n samples -> y: n samples. */
    void down (int ch, const float* w, float* y, int n) noexcept;

private:
    struct Ring
    {
        std::vector<double> buf;
        int len = 0, pos = 0;
        void init (int l) { len = l; buf.assign ((size_t) (2 * l), 0.0); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
        /** Pushes x and returns the window (oldest first, len samples). */
        const double* push (double x) noexcept
        {
            buf[(size_t) pos] = x;
            buf[(size_t) (pos + len)] = x;
            const double* w = buf.data() + pos + 1;
            if (++pos == len) pos = 0;
            return w;
        }
    };

    int k = 0;
    std::vector<double> c;           // interpolator side taps c_j (j = 1..K+1)
    std::vector<Ring> upHist, dnEven, dnOdd;
};

/** Linear-phase oversampler (1x, 2x, 4x or 8x) with exact integer latency.

    Stage s converts between 2^s and 2^(s+1) times the base rate. Its round
    trip delays by 2K+1 samples at rate 2^s; stages above the first are padded
    to a multiple of 2^s samples so the total latency is a whole number of
    base-rate samples. */
class LinearPhaseOversampler
{
public:
    static constexpr int kMaxLog2 = 3;

    void prepare (int numChannels, int factorLog2, int maxBlockSize);
    void reset();

    int factor() const noexcept { return 1 << log2f; }
    int latencySamples() const noexcept { return latency; }

    /** Upsamples into internal buffers and returns per-channel pointers. */
    float* const* up (const float* const* in, int numChannels, int n) noexcept;
    /** Downsamples the internal buffers into out. */
    void down (float* const* out, int numChannels, int n) noexcept;

private:
    struct PadDelay
    {
        std::vector<float> buf;
        int pos = 0;
        void process (float* x, int n) noexcept
        {
            if (buf.empty()) return;
            const int len = (int) buf.size();
            for (int i = 0; i < n; ++i)
            {
                const float y = buf[(size_t) pos];
                buf[(size_t) pos] = x[i];
                x[i] = y;
                if (++pos == len) pos = 0;
            }
        }
    };

    int log2f = 0, channels = 2, latency = 0;
    HalfbandStage stages[kMaxLog2];
    int pad[kMaxLog2] {};
    // level[s][ch]: buffer at rate 2^s (s = 1..log2f); level 0 is the caller's data
    std::vector<std::vector<float>> level[kMaxLog2 + 1];
    std::vector<PadDelay> padDelay[kMaxLog2];
    std::vector<float*> osPtr;
};

} // namespace gkd::dsp
