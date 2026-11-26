// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Modules.h"
#include <vector>

namespace gkd::dsp
{

/** Phase distortion: the signal modulates its own phase. Each sample is read
    from a short delay whose length follows a smoothed ("tone") copy of the
    signal, so the waveform is warped in time instead of clipped in level -
    FM-like sidebands, especially rich on bass. The amplitude stays the same.
    delay = 2 + depth * (1 + tanh (2 * lowpass (x))) samples; Hermite read. */
class PhaseDistorter
{
public:
    static constexpr double kMaxDepthMs = 4.0;

    /** Allocates the delay lines (call from prepareToPlay, never on the audio thread). */
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        len = (int) std::ceil (2.0 * kMaxDepthMs * 0.001 * fs) + 8;
        for (auto& b : buf) b.assign ((size_t) len, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        pos = 0;
        lp[0] = lp[1] = 0.0;
    }

    /** Modulation depth (samples) for a drive setting: 0 dB -> none, 48 dB -> 4 ms. */
    double depthForDrive (double driveDb) const noexcept
    {
        const double t = juce::jlimit (0.0, 1.0, driveDb / 48.0);
        return t * t * kMaxDepthMs * 0.001 * fs;
    }

    /** Low-pass coefficient of the modulator for a bias ("tone") setting: 125 Hz .. 8 kHz. */
    double toneCoefficient (double bias) const noexcept
    {
        const double fc = 1000.0 * std::pow (2.0, 3.0 * juce::jlimit (-1.0, 1.0, bias));
        return 1.0 - std::exp (-6.283185307179586 * std::min (fc, 0.45 * fs) / fs);
    }

    /** Processes x in place; depth (samples) and tone ramp linearly across the block. */
    void process (double* const* x, int nch, int n, double depth0, double depth1, double tone0, double tone1) noexcept
    {
        if (n <= 0 || buf[0].empty()) return;
        const double maxDepth = 0.5 * (double) (len - 8);
        const double inv = 1.0 / (double) n;
        for (int i = 0; i < n; ++i)
        {
            const double k = (double) (i + 1) * inv;
            const double depth = juce::jlimit (0.0, maxDepth, depth0 + (depth1 - depth0) * k);
            const double a = tone0 + (tone1 - tone0) * k;
            for (int c = 0; c < nch; ++c)
            {
                auto& b = buf[(size_t) c];
                const double in = x[c][i];
                b[(size_t) pos] = (float) juce::jlimit (-16.0, 16.0, in);
                lp[c] += a * (in - lp[c]);
                if (! std::isfinite (lp[c])) lp[c] = 0.0;
                const double m = fastTanh (2.0 * lp[c]);                 // -1 .. 1
                const double d = 2.0 + depth * (1.0 + m);                 // >= 2: the Hermite read stays causal
                double r = (double) pos - d;
                while (r < 0.0) r += (double) len;
                const int i1 = (int) r;
                const double f = r - (double) i1;
                auto at = [&b, this] (int idx) { return (double) b[(size_t) ((idx % len + len) % len)]; };
                const double y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
                // 4-point Hermite
                const double c1 = 0.5 * (y2 - y0);
                const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
                const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
                x[c][i] = ((c3 * f + c2) * f + c1) * f + y1;
            }
            if (++pos >= len) pos = 0;
        }
    }

private:
    double fs = 48000.0;
    int len = 0, pos = 0;
    std::vector<float> buf[2];
    double lp[2] {};
};

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
        // One specialised loop per effect: no per-sample dispatch.
        switch (type)
        {
            case FxType::Phaser:  run<FxType::Phaser>  (rateHz, depth, feedback, mix, x, nch, n); break;
            case FxType::Flanger: run<FxType::Flanger> (rateHz, depth, feedback, mix, x, nch, n); break;
            case FxType::Chorus:  run<FxType::Chorus>  (rateHz, depth, feedback, mix, x, nch, n); break;
            case FxType::Vibrato: run<FxType::Vibrato> (rateHz, depth, feedback, mix, x, nch, n); break;
            case FxType::Tremolo: run<FxType::Tremolo> (rateHz, depth, feedback, mix, x, nch, n); break;
            case FxType::AutoPan: run<FxType::AutoPan> (rateHz, depth, feedback, mix, x, nch, n); break;
            case FxType::Off:
            case FxType::Count:
            default: break;
        }
        cRate = rateHz; cDepth = depth; cFb = feedback; cMix = mix;
        // safety net, once per block: a non-finite state restarts the effect
        for (int c = 0; c < 2; ++c)
        {
            bool ok = std::isfinite (fbState[c]);
            for (double v : ap[c]) ok = ok && std::isfinite (v);
            if (! ok) { fbState[c] = 0.0; for (auto& v : ap[c]) v = 0.0; }
        }
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
    template <FxType T>
    void run (double rateHz, double depth, double feedback, double mix, double* const* x, int nch, int n) noexcept
    {
        const double inv = 1.0 / (double) n;
        const double dRate = (rateHz - cRate) * inv, dDepth = (depth - cDepth) * inv;
        const double dFb = (feedback - cFb) * inv, dMix = (mix - cMix) * inv;
        const int len = (int) delay[0].size();
        constexpr double twoPi = 6.283185307179586;
        const double invFs = 1.0 / fs;

        for (int i = 0; i < n; ++i)
        {
            const double k = (double) (i + 1);
            const double rate = cRate + dRate * k, dep = cDepth + dDepth * k;
            const double fb = cFb + dFb * k, m = cMix + dMix * k;
            phase += rate * invFs;
            if (phase >= 1.0) phase -= std::floor (phase);

            // LFO (sine, at most 40 Hz) evaluated every 8 samples and interpolated.
            // The right channel runs a quarter cycle later: stereo movement.
            const bool control = (ctl & 7) == 0;
            if (control)
                for (int c = 0; c < 2; ++c)
                {
                    const double target = std::sin (twoPi * (phase + 8.0 * rate * invFs + 0.25 * c));
                    lfoInc[c] = (target - lfoCur[c]) * 0.125;
                }
            lfoCur[0] += lfoInc[0];
            lfoCur[1] += lfoInc[1];
            const double lfo0 = lfoCur[0];

            for (int c = 0; c < nch; ++c)
            {
                const double lfo = lfoCur[c];                    // -1..1
                const double uni = 0.5 + 0.5 * lfo;              // 0..1
                const double in = x[c][i];
                double fx;

                if constexpr (T == FxType::Phaser)
                {
                    // Six first-order all-pass stages swept between 160 Hz and up to ~10 kHz;
                    // the coefficient is refreshed every 8 samples (the sweep is at most 40 Hz).
                    if (control)
                    {
                        const double fc = 160.0 * std::exp (uni * (1.0 + 5.0 * dep) * 0.6931471805599453);
                        const double w = 3.141592653589793 * std::min (fc, 0.45 * fs) * invFs;
                        const double t = w < 0.8 ? tanPade (w) : std::tan (w);
                        apCoef[c] = (t - 1.0) / (t + 1.0);
                    }
                    const double a = apCoef[c];
                    double* st = ap[c];
                    double v = in + fb * fbState[c];
                    for (int s = 0; s < 6; ++s)
                    {
                        const double y = a * v + st[s];
                        st[s] = v - a * y;
                        v = y;
                    }
                    fbState[c] = v;
                    fx = 0.5 * (in + v);
                }
                else if constexpr (T == FxType::Flanger || T == FxType::Chorus || T == FxType::Vibrato)
                {
                    double ms;
                    if constexpr (T == FxType::Flanger)     ms = 0.3 + dep * 6.0 * uni;
                    else if constexpr (T == FxType::Chorus) ms = 11.0 + dep * 9.0 * uni;
                    else                                    ms = 0.2 + dep * 5.0 * uni;
                    const double d = std::clamp (ms * 0.001 * fs, 1.0, (double) (len - 3));
                    double rp = (double) wpos - d;
                    if (rp < 0.0) rp += (double) len;
                    const int i0 = (int) rp;
                    const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
                    const double fr = rp - (double) i0;
                    auto& buf = delay[(size_t) c];
                    const double del = (double) buf[(size_t) i0] + fr * ((double) buf[(size_t) i1] - (double) buf[(size_t) i0]);
                    double fbAmt;
                    if constexpr (T == FxType::Vibrato)     fbAmt = 0.0;
                    else if constexpr (T == FxType::Chorus) fbAmt = 0.35 * fb;
                    else                                    fbAmt = fb;
                    double w = in + fbAmt * del;
                    if (! (w > -16.0)) w = -16.0;   // also catches NaN
                    if (w > 16.0) w = 16.0;
                    buf[(size_t) wpos] = (float) w;
                    if constexpr (T == FxType::Vibrato) fx = del;
                    else                                fx = 0.5 * (in + del);
                }
                else if constexpr (T == FxType::Tremolo)
                {
                    fx = in * (1.0 - dep * (0.5 + 0.5 * lfo0));
                }
                else   // AutoPan
                {
                    const double g = c == 0 ? 1.0 - dep * (0.5 + 0.5 * lfo0) : 1.0 - dep * (0.5 - 0.5 * lfo0);
                    fx = nch > 1 ? in * g : in;
                }
                x[c][i] = in + m * (fx - in);
            }
            if (++wpos >= len) wpos = 0;
            ++ctl;
        }
    }

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
