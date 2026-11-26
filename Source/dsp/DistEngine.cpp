// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DistEngine.h"
#include <cstring>

namespace gkd::dsp
{

void DistEngine::prepare (double newHostRate, int maxBlockSize, int numChannels, int oversamplingLog2)
{
    hostRate = newHostRate;
    osLog2 = juce::jlimit (0, LinearPhaseOversampler::kMaxLog2, oversamplingLog2);
    osFactor = 1 << osLog2;
    osRate = hostRate * osFactor;
    channels = juce::jlimit (1, 2, numChannels);
    maxBlock = std::max (1, maxBlockSize);

    os.prepare (channels, osLog2, maxBlock);
    for (int b = 0; b < kNumBands; ++b)
        bands[b].prepare (hostRate, osFactor, b);
    gComp.prepare (osRate);
    ott.prepare (osRate);
    gNoise[0].prepare (osRate, 0xabcdef01u);
    gNoise[1].prepare (osRate, 0x10fedcbau);
    limiter.prepare (hostRate, channels, lookaheadFor (hostRate));

    const int dryLen = os.latencySamples() + 1;
    for (int c = 0; c < 2; ++c)
    {
        dryRing[c].assign ((size_t) dryLen, 0.0f);
        dryBuf[c].assign ((size_t) maxBlock, 0.0f);
    }
    reset();
}

void DistEngine::reset()
{
    os.reset();
    for (auto& b : bands) b.reset();
    hp.clear();
    lp.clear();
    gComp.reset();
    ott.reset();
    gNoise[0].reset();
    gNoise[1].reset();
    limiter.reset();
    for (auto& r : dryRing) std::fill (r.begin(), r.end(), 0.0f);
    dryPos = 0;
    initialised = false;
    for (auto& v : filterKey) v = -1.0;
    soloMix = 0.0;
}

void DistEngine::designGlobalFilters (bool snap)
{
    const double k[3] { sHp, sLp, (double) filtSlope };
    if (std::memcmp (k, filterKey, sizeof (k)) == 0) return;
    const bool fresh = filterKey[2] < -0.5;
    std::memcpy (filterKey, k, sizeof (k));

    DesignParams d;
    d.q = kDefaultQ;
    d.slopeIdx = filtSlope == 0 ? 1 : 3;     // 12 or 24 dB/oct Butterworth
    d.type = FilterType::LowCut;
    d.freq = std::exp (sHp);
    hp.setTarget (designDigital (designAnalog (d, osRate)), snap || fresh);
    d.type = FilterType::HighCut;
    d.freq = std::exp (sLp);
    lp.setTarget (designDigital (designAnalog (d, osRate)), snap || fresh);
}

void DistEngine::updateGlobal (const GlobalParams& g, int hostCount)
{
    const double alpha = 1.0 - std::exp (-(hostCount / hostRate) / 0.02);
    if (! initialised)
    {
        sHp = std::log (g.hpFreq);
        sLp = std::log (g.lpFreq);
        hpMix = g.hpOn ? 1.0 : 0.0;
        lpMix = g.lpOn ? 1.0 : 0.0;
        compMix = g.compOn ? 1.0 : 0.0;
        compModeNow = g.compMode;
        sOttLowX = std::log (g.ottLowX);
        sOttHighX = std::log (g.ottHighX);
        clipMix = g.clipOn ? 1.0 : 0.0;
        noiseLvl = g.noiseDb > kNoiseOffDb + 0.05 ? dbToLin (g.noiseDb) : 0.0;
        mixNow = g.mix;
        outNow = dbToLin (g.outGainDb);
        filtSlope = g.filtSlope;
        designGlobalFilters (true);
        initialised = true;
    }
    else
    {
        sHp += alpha * (std::log (g.hpFreq) - sHp);
        sLp += alpha * (std::log (g.lpFreq) - sLp);
        if (std::abs (sHp - std::log (g.hpFreq)) < 1.0e-5) sHp = std::log (g.hpFreq);
        if (std::abs (sLp - std::log (g.lpFreq)) < 1.0e-5) sLp = std::log (g.lpFreq);
        filtSlope = g.filtSlope;
        designGlobalFilters (false);
        sOttLowX += alpha * (std::log (g.ottLowX) - sOttLowX);
        sOttHighX += alpha * (std::log (g.ottHighX) - sOttHighX);
    }

    hpMixT = g.hpOn ? 1.0 : 0.0;
    lpMixT = g.lpOn ? 1.0 : 0.0;
    // Switching between standard and OTT: fade the compressor out, switch, fade back in.
    compMixT = g.compOn && g.compMode == compModeNow ? 1.0 : 0.0;
    if (g.compMode != compModeNow && compMix <= 0.0)
    {
        compModeNow = g.compMode;
        gComp.reset();
        ott.reset();
        compMixT = g.compOn ? 1.0 : 0.0;
    }
    {
        OttCompressor::Settings st;
        st.lowX = std::exp (sOttLowX); st.highX = std::exp (sOttHighX);
        st.depth = g.ottDepth; st.time = g.ottTime; st.up = g.ottUp; st.down = g.ottDown;
        st.inDb = g.ottInDb; st.outDb = g.ottOutDb;
        ott.set (st);
    }
    clipMixT = g.clipOn ? 1.0 : 0.0;
    noiseLvlT = g.noiseDb > kNoiseOffDb + 0.05 ? dbToLin (g.noiseDb) : 0.0;
    gCompOnNow = g.compOn || compMix > 0.0;
    gComp.set (g.compThresh, g.compRatio, g.compAttack, g.compRelease, g.compMakeup);
    clipCeil = dbToLin (g.clipCeiling);
    clipSoft = g.clipSoft;
    gNoiseType = g.noiseType;
}

void DistEngine::process (float* const* io, int numChannels, int numSamples, const EngineInput& in)
{
    const int nch = std::min (numChannels, channels);
    if (nch <= 0 || numSamples <= 0) return;
    jassert (numSamples <= maxBlock);
    numSamples = std::min (numSamples, maxBlock);
    const bool stereo = nch > 1;
    const GlobalParams& g = *in.global;

    // Sanitise the input (a host sending NaN / inf must never poison the filters).
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = io[c][i];
            io[c][i] = std::isfinite (v) ? juce::jlimit (-64.0f, 64.0f, v) : 0.0f;
        }

    // Dry copy delayed by the oversampler latency, for the dry/wet mix.
    {
        const int len = (int) dryRing[0].size();
        for (int c = 0; c < nch; ++c)
        {
            int p = dryPos;
            for (int i = 0; i < numSamples; ++i)
            {
                dryRing[c][(size_t) p] = io[c][i];
                if (++p == len) p = 0;
                dryBuf[c][(size_t) i] = dryRing[c][(size_t) p];   // oldest = latency samples ago
            }
        }
        dryPos = (dryPos + numSamples) % len;
    }

    float* const* data = os.up (io, nch, numSamples);
    const int solo = [&]
    {
        for (int b = 0; b < kNumBands; ++b)
            if (in.bands[b].used && in.bands[b].solo) return b;
        return -1;
    }();

    for (int pos = 0; pos < numSamples; pos += kSub)
    {
        const int hn = std::min (kSub, numSamples - pos);
        const int on = hn * osFactor;
        const int opos = pos * osFactor;

        updateGlobal (g, hn);
        if (in.mod != nullptr && in.modSlots != nullptr)
        {
            const float* scl = in.sidechain != nullptr && in.scChannels > 0 ? in.sidechain[0] + pos : nullptr;
            const float* scr = in.sidechain != nullptr && in.scChannels > 1 ? in.sidechain[1] + pos : nullptr;
            in.mod->advance (hn, io[0] + pos, stereo ? io[1] + pos : nullptr, scl, scr, g, in.modSlots, bandMods);
        }
        else
        {
            for (auto& m : bandMods) m = BandMod();
        }
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = in.bands[b];
            const float* dl = io[0] + pos;
            const float* dr = stereo ? io[1] + pos : nullptr;
            if (p.dynSC && in.sidechain != nullptr && in.scChannels > 0)
            {
                dl = in.sidechain[0] + pos;
                dr = in.scChannels > 1 ? in.sidechain[1] + pos : nullptr;
            }
            bands[b].update (p, bandMods[b], hn, dl, dr);
        }

        float* oL = data[0] + opos;
        float* oR = stereo ? data[1] + opos : nullptr;
        for (int i = 0; i < on; ++i)
        {
            xL[i] = oL[i];
            xR[i] = oR != nullptr ? (double) oR[i] : 0.0;
        }

        // Bands, in series
        if (solo >= 0)
        {
            std::fill (solL, solL + on, 0.0);
            std::fill (solR, solR + on, 0.0);
        }
        for (int b = 0; b < kNumBands; ++b)
            if (bands[b].audible())
                bands[b].process (xL, xR, on, stereo, scratch, b == solo ? solL : nullptr, b == solo ? solR : nullptr);
            else
                bands[b].process (xL, xR, 0, stereo, scratch, nullptr, nullptr);   // keeps its ramps in sync

        // Waveform view: input range and how much each band changed this block.
        if (in.waveOut != nullptr && in.waveCount != nullptr && *in.waveCount < in.waveCapacity)
        {
            auto& col = in.waveOut[(*in.waveCount)++];
            float lo = 0.0f, hi = 0.0f;
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < hn; ++i)
                {
                    const float v = dryBuf[c][(size_t) (pos + i)];
                    lo = std::min (lo, v); hi = std::max (hi, v);
                }
            col.inMin = lo; col.inMax = hi;
            col.outMin = col.outMax = 0.0f;   // filled by the processor from the final output
            for (int b = 0; b < kNumBands; ++b) col.effect[b] = bands[b].takeEffectRms();
        }
        else
            for (auto& b : bands) b.takeEffectRms();

        // Solo: listen to one band's output only
        if (solo >= 0 || soloMix > 0.0)
        {
            const double target = solo >= 0 ? 1.0 : 0.0;
            const double step = 1.0 / (0.005 * osRate);
            for (int i = 0; i < on; ++i)
            {
                soloMix = target > soloMix ? std::min (target, soloMix + step) : std::max (target, soloMix - step);
                xL[i] += soloMix * (solL[i] - xL[i]);
                xR[i] += soloMix * (solR[i] - xR[i]);
            }
        }

        // Global section (each module cross-fades when switched on / off)
        const double inv = 1.0 / (double) on;
        auto filterStage = [&] (FilterChain& f, double& mix, double target)
        {
            if (mix <= 0.0 && target <= 0.0) { f.commit(); return; }
            std::memcpy (tmpL, xL, sizeof (double) * (size_t) on);
            std::memcpy (tmpR, xR, sizeof (double) * (size_t) on);
            f.process (0, tmpL, on);
            if (stereo) f.process (1, tmpR, on);
            f.commit();
            const double dm = (target - mix) * inv;
            for (int i = 0; i < on; ++i)
            {
                const double m = mix + dm * (double) (i + 1);
                xL[i] += m * (tmpL[i] - xL[i]);
                xR[i] += m * (tmpR[i] - xR[i]);
            }
            mix = target;
        };
        filterStage (hp, hpMix, hpMixT);
        filterStage (lp, lpMix, lpMixT);

        if (compMix > 0.0 || compMixT > 0.0)
        {
            std::memcpy (tmpL, xL, sizeof (double) * (size_t) on);
            std::memcpy (tmpR, xR, sizeof (double) * (size_t) on);
            if (compModeNow == 1) ott.process (tmpL, tmpR, on, stereo);
            else                  gComp.process (tmpL, tmpR, on, stereo);
            const double dm = (compMixT - compMix) * inv;
            for (int i = 0; i < on; ++i)
            {
                const double m = compMix + dm * (double) (i + 1);
                xL[i] += m * (tmpL[i] - xL[i]);
                xR[i] += m * (tmpR[i] - xR[i]);
            }
            compMix = compMixT;
            if (compMix <= 0.0) { gComp.reset(); ott.reset(); }
        }

        if (noiseLvl > 0.0 || noiseLvlT > 0.0)
        {
            const double dn = (noiseLvlT - noiseLvl) * inv;
            for (int i = 0; i < on; ++i)
            {
                const double lvl = noiseLvl + dn * (double) (i + 1);
                xL[i] += lvl * gNoise[0].next (gNoiseType);
                if (stereo) xR[i] += lvl * gNoise[1].next (gNoiseType);
            }
            noiseLvl = noiseLvlT;
        }

        if (clipMix > 0.0 || clipMixT > 0.0)
        {
            const double c = clipCeil, invC = 1.0 / clipCeil;
            const double dm = (clipMixT - clipMix) * inv;
            for (int i = 0; i < on; ++i)
            {
                const double m = clipMix + dm * (double) (i + 1);
                const double cl = clipSoft ? c * fastTanh (xL[i] * invC) : std::clamp (xL[i], -c, c);
                const double cr = clipSoft ? c * fastTanh (xR[i] * invC) : std::clamp (xR[i], -c, c);
                xL[i] += m * (cl - xL[i]);
                xR[i] += m * (cr - xR[i]);
            }
            clipMix = clipMixT;
        }

        for (int i = 0; i < on; ++i)
        {
            oL[i] = (float) (std::isfinite (xL[i]) ? xL[i] : 0.0);
            if (oR != nullptr) oR[i] = (float) (std::isfinite (xR[i]) ? xR[i] : 0.0);
        }
    }

    os.down (io, nch, numSamples);

    // Dry / wet mix and output gain (host rate)
    {
        const double mixT = g.mix, outT = dbToLin (g.outGainDb);
        const double invN = 1.0 / (double) numSamples;
        const double dm = (mixT - mixNow) * invN, dg = (outT - outNow) * invN;
        for (int i = 0; i < numSamples; ++i)
        {
            const double m = mixNow + dm * (double) (i + 1), og = outNow + dg * (double) (i + 1);
            for (int c = 0; c < nch; ++c)
            {
                const double d = dryBuf[c][(size_t) i];
                io[c][i] = (float) (og * (d + m * ((double) io[c][i] - d)));
            }
        }
        mixNow = mixT;
        outNow = outT;
    }

    limiter.process (io, nch, numSamples, g.limitOn, g.limitCeiling, g.limitRelease);

    for (int b = 0; b < kNumBands; ++b)
    {
        if (in.dynAmountOut != nullptr) in.dynAmountOut[b] = bands[b].dynAmount();
        if (in.compGrOut != nullptr) in.compGrOut[b] = bands[b].compGainReduction();
        if (in.telemetryOut != nullptr) in.telemetryOut[b] = bands[b].telemetry();
    }
}

} // namespace gkd::dsp
