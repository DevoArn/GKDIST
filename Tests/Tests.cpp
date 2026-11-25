// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

// Numerical verification of the GKDIST DSP core.
#include "dsp/DistEngine.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace gkd;
using namespace gkd::dsp;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// ---------------------------------------------------------------------------
struct Setup
{
    BandParams bands[kNumBands];
    GlobalParams g;
    float dyn[kNumBands] {}, gr[kNumBands] {};
    EngineInput in;
    Setup() { in.bands = bands; in.global = &g; in.dynAmountOut = dyn; in.compGrOut = gr; }

    BandParams& band (int i)
    {
        auto& b = bands[i];
        b.used = true; b.enabled = true;
        return b;
    }
};

struct Stereo
{
    std::vector<float> L, R;
    explicit Stereo (size_t n) : L (n, 0.0f), R (n, 0.0f) {}
};

static void run (DistEngine& e, Setup& s, Stereo& x, int block = 256, const Stereo* sc = nullptr)
{
    const int n = (int) x.L.size();
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = std::min (block, n - pos);
        float* io[2] { x.L.data() + pos, x.R.data() + pos };
        const float* scp[2] {};
        if (sc != nullptr) { scp[0] = sc->L.data() + pos; scp[1] = sc->R.data() + pos; s.in.sidechain = scp; s.in.scChannels = 2; }
        e.process (io, 2, len, s.in);
        s.in.sidechain = nullptr;
        s.in.scChannels = 0;
    }
}

static Stereo sine (double fs, int n, std::initializer_list<std::pair<double, double>> partials)   // (freq, amplitude)
{
    Stereo x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double v = 0.0;
        for (auto& p : partials) v += p.second * std::sin (2.0 * kPi * p.first * i / fs);
        x.L[(size_t) i] = x.R[(size_t) i] = (float) v;
    }
    return x;
}

/** Amplitude of a sinusoid at freq in x[start, start+len) (Hann-windowed DFT). */
static double toneAmp (const std::vector<float>& x, int start, int len, double freq, double fs)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (len - 1));
        acc += (double) x[(size_t) (start + i)] * w * std::polar (1.0, -2.0 * kPi * freq * i / fs);
        wsum += w;
    }
    return 2.0 * std::abs (acc) / wsum;
}

static double db (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-20)); }

static double rms (const std::vector<float>& x, int start, int len)
{
    double s = 0.0;
    for (int i = 0; i < len; ++i) s += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
    return std::sqrt (s / len);
}

static Stereo bandLimitedNoise (double fs, int n, uint32_t seed)
{
    // Sum of random sines below 15 kHz (oversampling filters legitimately
    // remove the last ~1 kHz below Nyquist).
    juce::Random rng ((juce::int64) seed);
    Stereo x ((size_t) n);
    for (int k = 0; k < 80; ++k)
    {
        const double f = 30.0 * std::pow (15000.0 / 30.0, rng.nextDouble());
        const double ph = rng.nextDouble() * 2.0 * kPi, ph2 = rng.nextDouble() * 2.0 * kPi;
        for (int i = 0; i < n; ++i)
        {
            x.L[(size_t) i] += (float) (0.012 * std::sin (2.0 * kPi * f * i / fs + ph));
            x.R[(size_t) i] += (float) (0.012 * std::sin (2.0 * kPi * f * i / fs + ph2));
        }
    }
    return x;
}

/** Max deviation of y from x delayed by lat, and the best-matching delay. */
static std::pair<double, int> nullTest (const Stereo& x, const Stereo& y, int lat, int skip)
{
    double err = 0.0;
    const int n = (int) x.L.size();
    for (int i = skip; i < n; ++i)
        err = std::max ({ err, (double) std::abs (y.L[(size_t) i] - x.L[(size_t) (i - lat)]),
                          (double) std::abs (y.R[(size_t) i] - x.R[(size_t) (i - lat)]) });
    int best = lat; double bestE = 1.0e30;
    for (int d = std::max (0, lat - 60); d < lat + 60; ++d)
    {
        double e2 = 0.0;
        for (int i = skip; i < skip + 4096 && i < n; ++i) { const double v = y.L[(size_t) i] - x.L[(size_t) (i - d)]; e2 += v * v; }
        if (e2 < bestE) { bestE = e2; best = d; }
    }
    return { err, best };
}

// ---------------------------------------------------------------------------
static void testShapers()
{
    std::printf ("[1] Distortion curves\n");
    const auto names = distTypeNames();
    for (int t = 0; t < (int) DistType::Count; ++t)
    {
        const auto type = (DistType) t;
        double maxAbs = 0.0;
        bool finite = true, silentOk = true;
        for (int i = -20000; i <= 20000; ++i)
        {
            const double u = (double) i * 0.75;    // up to +-15000 (48 dB drive on a +24 dBFS input)
            for (double bias : { -1.0, -0.3, 0.0, 0.5, 1.0 })
            {
                const double y = shapeBiased (type, u, bias);
                finite = finite && std::isfinite (y);
                maxAbs = std::max (maxAbs, std::abs (y));
            }
        }
        for (double bias = -1.0; bias <= 1.0; bias += 0.125)
            silentOk = silentOk && std::abs (shapeBiased (type, 0.0, bias)) < 1.0e-12;
        double agMin = 1e9, agMax = 0;
        for (double d = 0.0; d <= 48.0; d += 6.0)
            for (double bias : { -1.0, 0.0, 1.0 })
            {
                const double a = autoGainFactor (type, d, bias);
                agMin = std::min (agMin, a); agMax = std::max (agMax, a);
                finite = finite && std::isfinite (a);
            }
        std::printf ("  %-11s max |y| %.3f  auto-gain %.4f .. %.3f\n", names[t].toRawUTF8(), maxAbs, agMin, agMax);
        CHECK (finite, "%s produced a non-finite value", names[t].toRawUTF8());
        CHECK (maxAbs <= 2.0, "%s output not bounded (%.3f)", names[t].toRawUTF8(), maxAbs);
        CHECK (silentOk, "%s: silence does not stay silent with bias", names[t].toRawUTF8());
    }
    // Small-signal behaviour of the smooth curves: unity slope at zero
    for (auto t : { DistType::SoftClip, DistType::HardClip, DistType::Tube, DistType::Tape, DistType::Diode, DistType::Foldback })
    {
        const double slope = shapeCurve (t, 1.0e-6) / 1.0e-6;
        CHECK (std::abs (slope - 1.0) < 1.0e-3, "%s small-signal slope %.5f", distTypeNames()[(int) t].toRawUTF8(), slope);
    }
    CHECK (std::abs (fastTanh (0.7) - std::tanh (0.7)) < 2.0e-6 && std::abs (fastTanh (3.0) - std::tanh (3.0)) < 2.0e-5,
           "fastTanh accuracy");
}

static void testLatencyAndNull()
{
    std::printf ("[2] Latency and transparency (no bands / neutral bands)\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (int os = 0; os <= 3; ++os)
        {
            const int n = 1 << 15;
            const auto x = bandLimitedNoise (fs, n, 11);
            DistEngine e;
            e.prepare (fs, 512, 2, os);
            const int lat = e.latencySamples();

            Setup s;
            Stereo y = x;
            run (e, s, y, 333);
            auto [err, best] = nullTest (x, y, lat, lat + 2048);

            // Neutral bands: every type at high drive with wet 0 must vanish.
            Setup s2;
            for (int b = 0; b < (int) DistType::Count; ++b)
            {
                auto& p = s2.band (b);
                p.type = (DistType) b; p.driveDb = 36.0; p.wet = 0.0; p.freq = 200.0 * (b + 1); p.q = 2.0;
                p.shape = (Shape) (b % 4);
            }
            DistEngine e2;
            e2.prepare (fs, 512, 2, os);
            Stereo y2 = x;
            run (e2, s2, y2, 256);
            auto [err2, best2] = nullTest (x, y2, lat, lat + 2048);

            std::printf ("  %6.0f Hz  os %dx  latency %3d (measured %3d)  null %.1f dB, neutral bands %.1f dB\n",
                         fs, 1 << os, lat, best, db (err), db (err2));
            CHECK (best == lat && best2 == lat, "latency mismatch at %g Hz, os %d", fs, os);
            CHECK (db (err) < (os == 0 ? -140.0 : -70.0), "no-band null %.1f dB (fs %g, os %d)", db (err), fs, os);
            CHECK (db (err2) < (os == 0 ? -120.0 : -70.0), "neutral-band null %.1f dB (fs %g, os %d)", db (err2), fs, os);
        }
    }
}

static void testIsolation()
{
    std::printf ("[3] Band isolation\n");
    const double fs = 48000.0;
    const int n = 48000;
    const auto x = sine (fs, n, { { 100.0, 0.25 }, { 3000.0, 0.25 } });
    Setup s;
    auto& p = s.band (0);
    p.shape = Shape::Bell; p.freq = 3000.0; p.q = 2.0; p.type = DistType::HardClip; p.driveDb = 30.0; p.wet = 1.0;
    DistEngine e;
    e.prepare (fs, 512, 2, 2);
    Stereo y = x;
    run (e, s, y);
    const int lat = e.latencySamples(), start = n / 2, len = 8192;
    const double in100 = toneAmp (x.L, start - lat, len, 100.0, fs), out100 = toneAmp (y.L, start, len, 100.0, fs);
    const double out9k = toneAmp (y.L, start, len, 9000.0, fs), out3k = toneAmp (y.L, start, len, 3000.0, fs);
    std::printf ("  100 Hz (outside the band) changed by %.2f dB; 3 kHz %.1f dB; 3rd harmonic 9 kHz %.1f dB re 3 kHz\n",
                 db (out100 / in100), db (out3k / 0.25), db (out9k / out3k));
    CHECK (std::abs (db (out100 / in100)) < 0.5, "tone outside the band changed by %.2f dB", db (out100 / in100));
    CHECK (db (out9k / out3k) > -30.0, "no distortion harmonics in the band");
}

static void testAliasing()
{
    std::printf ("[4] Aliasing (hard clip, 7.1 kHz, 24 dB drive)\n");
    const double fs = 48000.0;
    const int n = 1 << 15;
    const auto x = sine (fs, n, { { 7100.0, 0.5 } });
    double alias[4] {};
    for (int os = 0; os <= 3; ++os)
    {
        Setup s;
        auto& p = s.band (0);
        p.shape = Shape::Full; p.type = DistType::HardClip; p.driveDb = 24.0; p.autoGain = false;
        DistEngine e;
        e.prepare (fs, 512, 2, os);
        Stereo y = x;
        run (e, s, y);
        const int start = n / 2, len = 8192;
        const double fund = toneAmp (y.L, start, len, 7100.0, fs);
        // 5th harmonic (35.5 kHz) folds to 12.5 kHz, 7th (49.7 kHz) to 1.7 kHz
        const double a1 = toneAmp (y.L, start, len, 12500.0, fs), a2 = toneAmp (y.L, start, len, 1700.0, fs);
        alias[os] = db (std::max (a1, a2) / fund);
        std::printf ("  os %dx: strongest alias %.1f dB below the fundamental\n", 1 << os, -alias[os]);
    }
    CHECK (alias[0] > -40.0, "test signal does not alias without oversampling (%.1f)", alias[0]);
    CHECK (alias[2] < -70.0 && alias[3] < -70.0, "oversampling does not suppress aliasing (4x %.1f, 8x %.1f)", alias[2], alias[3]);
}

static void testDynamics()
{
    std::printf ("[5] Dynamics (wet, drive, gain)\n");
    const double fs = 48000.0;
    const int n = 48000;
    auto runLevel = [&] (double amp, BandParams proto, float& amountOut) -> Stereo
    {
        Setup s;
        s.band (0) = proto;
        s.bands[0].used = true;
        DistEngine e;
        e.prepare (fs, 512, 2, 1);
        Stereo y = sine (fs, n, { { 1000.0, amp } });
        run (e, s, y);
        amountOut = s.dyn[0];
        return y;
    };
    BandParams p;
    p.used = true; p.shape = Shape::Bell; p.freq = 1000.0; p.q = 1.0; p.type = DistType::HardClip;
    p.driveDb = 0.0; p.wet = 0.0; p.autoGain = false;
    p.dynOn = true; p.dynThresh = -30.0; p.dynWet = 1.0; p.dynDrive = 30.0; p.dynAttack = 2.0; p.dynRelease = 50.0;

    float aq = 0, al = 0;
    const auto quiet = runLevel (0.003, p, aq);
    const auto loud = runLevel (0.5, p, al);
    const auto quietRef = sine (fs, n, { { 1000.0, 0.003 } });
    const int lat = DistEngine::lookaheadFor (fs) + 0, start = n / 2, len = 8192;
    juce::ignoreUnused (lat);
    const double qh = toneAmp (quiet.L, start, len, 3000.0, fs) / toneAmp (quiet.L, start, len, 1000.0, fs);
    const double lh = toneAmp (loud.L, start, len, 3000.0, fs) / toneAmp (loud.L, start, len, 1000.0, fs);
    std::printf ("  quiet: amount %.2f, 3rd harmonic %.1f dB;  loud: amount %.2f, 3rd harmonic %.1f dB\n", aq, db (qh), al, db (lh));
    CHECK (aq < 0.01 && db (qh) < -80.0, "quiet signal is processed (amount %.2f, H3 %.1f dB)", aq, db (qh));
    CHECK (al > 0.99 && db (lh) > -25.0, "loud signal does not engage wet/drive (amount %.2f, H3 %.1f dB)", al, db (lh));
    juce::ignoreUnused (quietRef);

    // Dynamic gain: wet 0, -12 dB range -> loud 1 kHz tone drops by 12 dB
    BandParams pg = p;
    pg.dynWet = 0.0; pg.dynDrive = 0.0; pg.dynGain = -12.0;
    float ag = 0;
    const auto ducked = runLevel (0.5, pg, ag);
    const double change = db (toneAmp (ducked.L, start, len, 1000.0, fs) / 0.5);
    std::printf ("  dynamic gain -12 dB on a loud tone: %.2f dB\n", change);
    CHECK (std::abs (change + 12.0) < 0.3, "dynamic gain %.2f dB instead of -12", change);

    // External side-chain: loud side-chain engages the band while the input is quiet
    {
        Setup s;
        s.band (0) = p;
        s.bands[0].used = true;
        s.bands[0].dynSC = true;
        DistEngine e;
        e.prepare (fs, 512, 2, 1);
        Stereo y = sine (fs, n, { { 1000.0, 0.003 } });
        const Stereo sc = sine (fs, n, { { 1000.0, 0.5 } });
        run (e, s, y, 256, &sc);
        std::printf ("  external side-chain: amount %.2f with a quiet input\n", s.dyn[0]);
        CHECK (s.dyn[0] > 0.99, "side-chain does not trigger dynamics");
    }
}

static void testCompressorClipLimiter()
{
    std::printf ("[6] Compressor, clipper, limiter\n");
    const double fs = 48000.0;
    const int n = 48000;
    {
        Setup s;
        auto& p = s.band (0);
        p.shape = Shape::Full; p.type = DistType::HardClip; p.driveDb = 0.0; p.autoGain = false;
        p.compOn = true; p.compThresh = -30.0; p.compRatio = 4.0; p.compAttack = 1.0; p.compRelease = 50.0;
        DistEngine e;
        e.prepare (fs, 512, 2, 0);
        Stereo y = sine (fs, n, { { 1000.0, 0.1 } });      // -20 dBFS peak, 10 dB over
        run (e, s, y);
        double pk = 0.0;
        for (int i = n / 2; i < n; ++i) pk = std::max (pk, (double) std::abs (y.L[(size_t) i]));
        std::printf ("  compressor -30 dB / 4:1 on a -20 dBFS tone: %.2f dBFS (expected -27.5)\n", db (pk));
        CHECK (std::abs (db (pk) + 27.5) < 0.5, "compressor output %.2f dBFS", db (pk));
    }
    for (int os : { 0, 2 })
    {
        Setup s;
        auto& p = s.band (0);
        p.shape = Shape::Full; p.type = DistType::HardClip; p.driveDb = 0.0; p.autoGain = false;
        p.clipMode = ClipMode::Hard; p.clipCeiling = -6.0;
        DistEngine e;
        e.prepare (fs, 512, 2, os);
        Stereo y = sine (fs, n, { { 997.0, 0.9 } });
        run (e, s, y);
        double pk = 0.0;
        for (int i = n / 4; i < n; ++i) pk = std::max (pk, (double) std::abs (y.L[(size_t) i]));
        std::printf ("  band hard clip at -6 dB, os %dx: peak %.2f dBFS\n", 1 << os, db (pk));
        CHECK (db (pk) <= -6.0 + (os == 0 ? 1.0e-4 : 1.0), "band clipper peak %.3f dBFS", db (pk));
    }
    for (int os : { 0, 2, 3 })
    {
        Setup s;
        s.g.limitOn = true; s.g.limitCeiling = -1.0; s.g.limitRelease = 50.0;
        auto& p = s.band (0);
        p.shape = Shape::Full; p.type = DistType::Fuzz; p.driveDb = 12.0; p.gainDb = 18.0;   // very hot
        DistEngine e;
        e.prepare (fs, 512, 2, os);
        Stereo y (48000);
        juce::Random rng (5);
        for (size_t i = 0; i < y.L.size(); ++i) { y.L[i] = (rng.nextFloat() * 2.f - 1.f) * 2.0f; y.R[i] = (rng.nextFloat() * 2.f - 1.f) * 2.0f; }
        run (e, s, y);
        double pk = 0.0;
        for (size_t i = 0; i < y.L.size(); ++i) pk = std::max ({ pk, (double) std::abs (y.L[i]), (double) std::abs (y.R[i]) });
        std::printf ("  limiter ceiling -1 dB, os %dx, +18 dB hot fuzz: peak %.4f dBFS, GR %.1f dB\n", 1 << os, db (pk), e.limiterGainReduction());
        CHECK (pk <= dbToLin (-1.0) * (1.0 + 1.0e-6), "limiter exceeded its ceiling (%.4f dBFS)", db (pk));
    }
}

static void testNoiseAndPhat()
{
    std::printf ("[7] Noise and Phat\n");
    for (int t = 0; t < (int) NoiseType::Count; ++t)
    {
        NoiseGen gen;
        gen.prepare (48000.0, 99);
        double s = 0.0;
        const int n = 48000 * 10;
        for (int i = 0; i < n; ++i) { const double v = gen.next ((NoiseType) t); s += v * v; }
        const double r = std::sqrt (s / n);
        std::printf ("  %-8s noise RMS %.2f dB\n", noiseTypeNames()[t].toRawUTF8(), db (r));
        CHECK (std::abs (db (r)) < 1.5, "%s noise not calibrated (%.2f dB)", noiseTypeNames()[t].toRawUTF8(), db (r));
    }

    const double fs = 48000.0;
    const int n = 96000;
    {
        Setup s;
        auto& p = s.band (0);
        p.shape = Shape::Full; p.type = DistType::HardClip; p.driveDb = 0.0; p.autoGain = false;
        p.noiseDb = -40.0; p.noiseType = NoiseType::White;
        DistEngine e;
        e.prepare (fs, 512, 2, 0);
        Stereo y (96000);
        run (e, s, y);
        const double r = rms (y.L, n / 2, n / 2);
        std::printf ("  band noise -40 dB on silence: %.2f dB RMS\n", db (r));
        CHECK (std::abs (db (r) + 40.0) < 1.5, "band noise level %.2f dB", db (r));

        Setup s2 = s;
        s2.in.bands = s2.bands; s2.in.global = &s2.g; s2.in.dynAmountOut = s2.dyn; s2.in.compGrOut = s2.gr;
        s2.bands[0].noiseFollow = 1.0;
        DistEngine e2;
        e2.prepare (fs, 512, 2, 0);
        Stereo y2 (96000);
        run (e2, s2, y2);
        std::printf ("  band noise with 100 %% follow on silence: %.1f dB RMS\n", db (rms (y2.L, n / 2, n / 2)));
        CHECK (db (rms (y2.L, n / 2, n / 2)) < -120.0, "follow does not gate the noise");
    }
    for (double amount : { 0.0, 1.0 })
    {
        Setup s;
        auto& p = s.band (0);
        p.shape = Shape::Full; p.type = DistType::HardClip; p.driveDb = 0.0; p.autoGain = false; p.phat = amount;
        DistEngine e;
        e.prepare (fs, 512, 2, 0);
        Stereo y = sine (fs, n, { { 100.0, 0.25 } });
        run (e, s, y);
        const double sub = toneAmp (y.L, n / 2, 32768, 50.0, fs) / toneAmp (y.L, n / 2, 32768, 100.0, fs);
        std::printf ("  phat %3.0f %%: sub-octave (50 Hz) %.1f dB re 100 Hz\n", amount * 100.0, db (sub));
        if (amount > 0.5) CHECK (db (sub) > -30.0, "phat produces no sub-octave (%.1f dB)", db (sub));
        else              CHECK (db (sub) < -90.0, "sub-octave without phat (%.1f dB)", db (sub));
    }
}

static void testSolo()
{
    std::printf ("[8] Solo\n");
    const double fs = 48000.0;
    const int n = 48000;
    const auto x = sine (fs, n, { { 100.0, 0.25 }, { 1000.0, 0.25 } });
    Setup s;
    auto& p = s.band (0);
    p.shape = Shape::Bell; p.freq = 1000.0; p.q = 2.0; p.wet = 0.0; p.solo = true;
    DistEngine e;
    e.prepare (fs, 512, 2, 0);
    Stereo y = x;
    run (e, s, y);
    const double r100 = toneAmp (y.L, n / 2, 8192, 100.0, fs) / 0.25, r1k = toneAmp (y.L, n / 2, 8192, 1000.0, fs) / 0.25;
    std::printf ("  soloed 1 kHz band: 1 kHz %.2f dB, 100 Hz %.1f dB\n", db (r1k), db (r100));
    CHECK (std::abs (db (r1k)) < 0.2 && db (r100) < -20.0, "solo does not isolate the band");
}

static void testAutomationStress()
{
    std::printf ("[9] Random automation of all 24 bands (stability)\n");
    for (int os : { 0, 2 })
    {
        const double fs = 48000.0;
        DistEngine e;
        e.prepare (fs, 512, 2, os);
        Setup s;
        juce::Random rng (2026);
        const int seconds = 20, block = 128;
        Stereo buf (block);
        double maxAbs = 0.0;
        bool finite = true;
        for (int blk = 0; blk < seconds * 48000 / block; ++blk)
        {
            if (blk % 3 == 0)
            {
                for (int b = 0; b < kNumBands; ++b)
                {
                    if (rng.nextFloat() > 0.3f) continue;
                    auto& p = s.bands[b];
                    p.used = rng.nextFloat() < 0.8f; p.enabled = rng.nextFloat() < 0.9f;
                    p.shape = (Shape) rng.nextInt ((int) Shape::Count);
                    p.freq = 20.0 * std::pow (1000.0, rng.nextDouble()); p.q = 0.1 * std::pow (200.0, rng.nextDouble());
                    p.slope = rng.nextInt (3);
                    p.type = (DistType) rng.nextInt ((int) DistType::Count);
                    p.driveDb = rng.nextDouble() * 48.0; p.bias = rng.nextDouble() * 2.0 - 1.0; p.autoGain = rng.nextBool();
                    p.filterType = (BandFilter) rng.nextInt ((int) BandFilter::Count); p.filterPost = rng.nextBool();
                    p.filterFreq = 20.0 * std::pow (1000.0, rng.nextDouble()); p.filterRes = 0.1 * std::pow (100.0, rng.nextDouble());
                    p.phat = rng.nextDouble(); p.noiseDb = -80.0 + rng.nextDouble() * 80.0;
                    p.noiseType = (NoiseType) rng.nextInt ((int) NoiseType::Count); p.noiseFollow = rng.nextDouble();
                    p.compOn = rng.nextBool(); p.compThresh = -60.0 * rng.nextDouble(); p.compRatio = 1.0 + 19.0 * rng.nextDouble();
                    p.compAttack = 0.1 + rng.nextDouble() * 100.0; p.compRelease = 5.0 + rng.nextDouble() * 500.0; p.compMakeup = 24.0 * rng.nextDouble();
                    p.clipMode = (ClipMode) rng.nextInt ((int) ClipMode::Count); p.clipCeiling = -24.0 * rng.nextDouble();
                    p.wet = rng.nextDouble(); p.gainDb = -24.0 + 48.0 * rng.nextDouble();
                    p.dynOn = rng.nextBool(); p.dynThresh = -80.0 + 86.0 * rng.nextDouble();
                    p.dynGain = -24.0 + 48.0 * rng.nextDouble(); p.dynDrive = -48.0 + 96.0 * rng.nextDouble(); p.dynWet = rng.nextDouble() * 2.0 - 1.0;
                    p.solo = rng.nextFloat() < 0.02f;
                }
                s.g.hpOn = rng.nextBool(); s.g.lpOn = rng.nextBool(); s.g.compOn = rng.nextBool(); s.g.clipOn = rng.nextBool();
                s.g.noiseDb = -80.0 + 60.0 * rng.nextDouble(); s.g.mix = rng.nextDouble();
                // Random settings may legitimately add up to +72 dB of gain; with the
                // output limiter on, nothing may pass its ceiling regardless.
                s.g.limitOn = true; s.g.limitCeiling = -0.3;
            }
            for (int i = 0; i < block; ++i)
            {
                buf.L[(size_t) i] = rng.nextFloat() - 0.5f;
                buf.R[(size_t) i] = rng.nextFloat() - 0.5f;
            }
            if (blk % 97 == 0) buf.L[0] = std::numeric_limits<float>::quiet_NaN();   // hostile input
            float* io[2] { buf.L.data(), buf.R.data() };
            e.process (io, 2, block, s.in);
            for (int i = 0; i < block; ++i)
            {
                finite = finite && std::isfinite (buf.L[(size_t) i]) && std::isfinite (buf.R[(size_t) i]);
                maxAbs = std::max ({ maxAbs, (double) std::abs (buf.L[(size_t) i]), (double) std::abs (buf.R[(size_t) i]) });
            }
        }
        std::printf ("  os %dx: %d s of random automation + NaN input: finite %s, max |y| %.1f\n", 1 << os, seconds, finite ? "yes" : "NO", maxAbs);
        CHECK (finite, "non-finite output under automation");
        CHECK (maxAbs <= dbToLin (-0.3) * (1.0 + 1.0e-6), "limiter ceiling exceeded under automation (%.4f)", maxAbs);
    }
}

static void testClickFreeSwitching()
{
    std::printf ("[10] Click-free type / shape switching\n");
    const double fs = 48000.0;
    const int n = 48000;
    Setup s;
    auto& p = s.band (0);
    p.shape = Shape::Full; p.type = DistType::SoftClip; p.driveDb = 6.0;
    DistEngine e;
    e.prepare (fs, 512, 2, 2);
    Stereo y = sine (fs, n, { { 220.0, 0.4 } });
    double maxStep = 0.0, steadyStep = 0.0;
    for (int pos = 0, k = 0; pos < n; pos += 256, ++k)
    {
        if (pos > 4096 && k % 10 == 0)
        {
            p.type = (DistType) ((int) p.type == (int) DistType::SoftClip ? (int) DistType::Tube : (int) DistType::SoftClip);
            p.shape = p.shape == Shape::Full ? Shape::Low : Shape::Full;
        }
        float* io[2] { y.L.data() + pos, y.R.data() + pos };
        e.process (io, 2, std::min (256, n - pos), s.in);
    }
    for (int i = 4096 + 1; i < n; ++i) maxStep = std::max (maxStep, (double) std::abs (y.L[(size_t) i] - y.L[(size_t) (i - 1)]));
    steadyStep = 2.0 * kPi * 220.0 / fs * 0.4 * 2.0;   // generous bound for a 220 Hz tone
    std::printf ("  largest sample step while switching: %.4f (smooth-signal bound %.4f)\n", maxStep, steadyStep);
    CHECK (maxStep < steadyStep, "discontinuity when switching (%.4f)", maxStep);
}

static void testPerformance()
{
    std::printf ("[11] CPU (stereo, 48 kHz, 256-sample blocks)\n");
    const double fs = 48000.0;
    auto bench = [&] (int os, int numBands, bool everything)
    {
        Setup s;
        for (int b = 0; b < numBands; ++b)
        {
            auto& p = s.band (b);
            p.freq = 40.0 * std::pow (400.0, b / (double) std::max (1, numBands - 1)); p.q = 1.5;
            p.type = (DistType) (b % ((int) DistType::Count - 1));
            if (everything)
            {
                p.filterType = BandFilter::LowPass; p.phat = 0.3; p.noiseDb = -60.0; p.compOn = true; p.clipMode = ClipMode::Soft;
                p.dynOn = true; p.dynDrive = 12.0;
            }
        }
        s.g.compOn = everything; s.g.limitOn = true; s.g.hpOn = true; s.g.clipOn = everything;
        DistEngine e;
        e.prepare (fs, 512, 2, os);
        const int seconds = 5;
        Stereo buf (256);
        juce::Random rng (3);
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int blk = 0; blk < seconds * 48000 / 256; ++blk)
        {
            for (int i = 0; i < 256; ++i) { buf.L[(size_t) i] = rng.nextFloat() - 0.5f; buf.R[(size_t) i] = rng.nextFloat() - 0.5f; }
            float* io[2] { buf.L.data(), buf.R.data() };
            e.process (io, 2, 256, s.in);
        }
        const double sec = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("  %2d bands%s, os %dx: %5.1f %% of one core\n", numBands, everything ? " (all modules)" : "", 1 << os, 100.0 * sec / seconds);
        return 100.0 * sec / seconds;
    };
    bench (0, 4, false);
    bench (2, 4, false);
    bench (2, 8, true);
    bench (1, 24, true);
    const double worst = bench (2, 24, true);
    bench (3, 24, true);
    CHECK (worst < 50.0, "24 bands with all modules at 4x need %.1f %% of a core", worst);
}

int main()
{
    juce::ScopedNoDenormals noDenormals;
    testShapers();
    testLatencyAndNull();
    testIsolation();
    testAliasing();
    testDynamics();
    testCompressorClipLimiter();
    testNoiseAndPhat();
    testSolo();
    testAutomationStress();
    testClickFreeSwitching();
    testPerformance();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
