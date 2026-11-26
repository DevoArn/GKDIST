// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Modules.h"
#include <vector>

namespace gkd::dsp
{

/** Per-band modulation effect: phaser, flanger, chorus, vibrato, tremolo,
    auto pan. Runs at the processing rate; every control (rate, depth,
    feedback, mix) is ramped across each block, so automation is click-free.
    out = x + mix * (effect(x) - x). */
class BandFx
{
public:
    static constexpr double kMaxDelayMs = 25.0;

    /** Allocates the delay lines (call from prepareToPlay, never on the audio thread). */
    void prepare (double sampleRate, uint32_t seed)
    {
        fs = sampleRate;
        const int len = (int) std::ceil (kMaxDelayMs * 0.001 * fs) + 8;
        for (auto& d : delay) d.assign ((size_t) len, 0.0f);
        phase = (seed % 1000u) / 1000.0;   // bands start at different LFO phases
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        wpos = 0;
        for (int c = 0; c < 2; ++c)
        {
            for (auto& v : ap[c]) v = 0.0;
            fbState[c] = 0.0;
        }
        ctl = 0;
        first = true;
    }

    /** x: the band signal (nch channels, n samples), processed in place. */
    void process (FxType type, double rateHz, double depth, double feedback, double mix,
                  double* const* x, int nch, int n) noexcept
    {
        if (type == FxType::Off || n <= 0 || delay[0].empty()) return;
        depth = std::clamp (depth, 0.0, 1.0);
        mix = std::clamp (mix, 0.0, 1.0);
        feedback = std::clamp (feedback, -0.95, 0.95);
        rateHz = std::clamp (rateHz, 0.005, 40.0);
        if (first)
        {
            cRate = rateHz; cDepth = depth; cFb = feedback; cMix = mix; first = false;
            for (int c = 0; c < 2; ++c) { lfoCur[c] = std::sin (6.283185307179586 * (phase + 0.25 * c)); lfoInc[c] = 0.0; }
            ctl = 0;
        }

        const double inv = 1.0 / (double) n;
        const double dRate = (rateHz - cRate) * inv, dDepth = (depth - cDepth) * inv;
        const double dFb = (feedback - cFb) * inv, dMix = (mix - cMix) * inv;
        const int len = (int) delay[0].size();
        constexpr double twoPi = 6.283185307179586;

        for (int i = 0; i < n; ++i)
        {
            const double k = (double) (i + 1);
            const double rate = cRate + dRate * k, dep = cDepth + dDepth * k;
            const double fb = cFb + dFb * k, m = cMix + dMix * k;
            phase += rate / fs;
            if (phase >= 1.0) phase -= std::floor (phase);

            // LFO (sine, at most 40 Hz) evaluated every 8 samples and interpolated.
            // The right channel runs a quarter cycle later: stereo movement.
            if ((ctl & 7) == 0)
                for (int c = 0; c < 2; ++c)
                {
                    const double target = std::sin (twoPi * (phase + 8.0 * rate / fs + 0.25 * c));
                    lfoInc[c] = (target - lfoCur[c]) * 0.125;
                }
            for (int c = 0; c < 2; ++c) lfoCur[c] += lfoInc[c];
            const double lfo0 = lfoCur[0];

            for (int c = 0; c < nch; ++c)
            {
                const double lfo = lfoCur[c];                    // -1..1
                const double uni = 0.5 + 0.5 * lfo;              // 0..1
                const double in = x[c][i];
                double fx = in;

                switch (type)
                {
                    case FxType::Phaser:
                    {
                        // Six first-order all-pass stages swept between 160 Hz and up to ~10 kHz.
                        // Coefficient refreshed every 8 samples (the sweep is at most 40 Hz).
                        if ((ctl & 7) == 0)
                        {
                            // Sweep 160 Hz .. 160 Hz * 2^(1 + 5 depth); ln 2 = 0.693...
                            const double fc = 160.0 * std::exp (uni * (1.0 + 5.0 * dep) * 0.6931471805599453);
                            const double w = 3.141592653589793 * std::min (fc, 0.45 * fs) / fs;
                            const double t = w < 0.8 ? tanPade (w) : std::tan (w);
                            apCoef[c] = (t - 1.0) / (t + 1.0);
                        }
                        const double a = apCoef[c];
                        double v = in + fb * fbState[c];
                        for (int s = 0; s < 6; ++s)
                        {
                            const double y = a * v + ap[c][s];
                            ap[c][s] = v - a * y;
                            v = y;
                        }
                        fbState[c] = std::isfinite (v) ? v : 0.0;
                        fx = 0.5 * (in + v);
                        break;
                    }
                    case FxType::Flanger:
                    case FxType::Chorus:
                    case FxType::Vibrato:
                    {
                        double ms;
                        if (type == FxType::Flanger)     ms = 0.3 + dep * 6.0 * uni;
                        else if (type == FxType::Chorus) ms = 11.0 + dep * 9.0 * uni;
                        else                             ms = 0.2 + dep * 5.0 * uni;
                        const double d = std::clamp (ms * 0.001 * fs, 1.0, (double) (len - 3));
                        double rp = (double) wpos - d;
                        while (rp < 0.0) rp += (double) len;
                        const int i0 = (int) rp;
                        const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
                        const double fr = rp - (double) i0;
                        const auto& buf = delay[(size_t) c];
                        const double del = (double) buf[(size_t) i0] + fr * ((double) buf[(size_t) i1] - (double) buf[(size_t) i0]);
                        const double fbAmt = type == FxType::Vibrato ? 0.0 : (type == FxType::Chorus ? 0.35 * fb : fb);
                        double w = in + fbAmt * del;
                        if (! std::isfinite (w)) w = 0.0;
                        delay[(size_t) c][(size_t) wpos] = (float) std::clamp (w, -16.0, 16.0);
                        fx = type == FxType::Vibrato ? del : 0.5 * (in + del);
                        break;
                    }
                    case FxType::Tremolo:
                        fx = in * (1.0 - dep * (0.5 + 0.5 * lfo0));
                        break;
                    case FxType::AutoPan:
                    {
                        const double pl = lfo0;
                        const double g = c == 0 ? 1.0 - dep * (0.5 + 0.5 * pl) : 1.0 - dep * (0.5 - 0.5 * pl);
                        fx = nch > 1 ? in * g : in;
                        break;
                    }
                    case FxType::Off:
                    case FxType::Count:
                    default:
                        break;
                }
                x[c][i] = in + m * (fx - in);
            }
            if (++wpos >= len) wpos = 0;
            ++ctl;
        }
        cRate = rateHz; cDepth = depth; cFb = feedback; cMix = mix;
    }

    /** tan (x) for 0 <= x < 0.8 (Pade [5/4], relative error < 1e-7). */
    static double tanPade (double x) noexcept
    {
        const double x2 = x * x;
        return x * (945.0 - 105.0 * x2 + x2 * x2) / (945.0 - 420.0 * x2 + 15.0 * x2 * x2);
    }

    /** Current LFO phase (0..1), for display. */
    double getPhase() const noexcept { return phase; }

private:
    double fs = 48000.0;
    std::vector<float> delay[2];
    int wpos = 0;
    double ap[2][6] {};
    double fbState[2] {};
    double apCoef[2] { -0.9, -0.9 };
    double lfoCur[2] {}, lfoInc[2] {};
    unsigned ctl = 0;
    double phase = 0.0;
    double cRate = 0.5, cDepth = 0.5, cFb = 0.0, cMix = 0.5;
    bool first = true;
};

} // namespace gkd::dsp
