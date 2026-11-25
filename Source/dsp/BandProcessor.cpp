// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BandProcessor.h"
#include <cstring>

namespace gkd::dsp
{

void BandProcessor::prepare (double newHostRate, int newOsFactor, int bandIndex)
{
    hostRate = newHostRate;
    osFactor = std::max (1, newOsFactor);
    osRate = hostRate * osFactor;
    index = bandIndex;

    dc.prepare (osRate);
    phat.prepare (osRate);
    noise[0].prepare (osRate, 0x1234567u + 7919u * (uint32_t) bandIndex);
    noise[1].prepare (osRate, 0x7654321u + 104729u * (uint32_t) bandIndex);
    comp.prepare (osRate);
    clipper.prepare (osRate);
    det.prepare (hostRate);
    noiseEnvRel = std::exp (-1.0 / (0.05 * osRate));
    fadeStep = 1.0 / (0.005 * osRate);   // 5 ms cross-fades
    reset();
}

void BandProcessor::reset()
{
    initialised = false;
    key = Key();
    keyCompOn = false;
    fade = fadeTarget = 0.0;
    amount = 0.0;
    region.clear();
    noiseRegion.clear();
    filt.clear();
    dc.reset();
    phat.reset();
    noise[0].reset();
    noise[1].reset();
    comp.reset();
    clipper.reset();
    det.reset();
    noiseEnv = 0.0;
    crushCount = 0;
    crushHeld[0] = crushHeld[1] = 0.0;
    for (auto& v : regionKey) v = -1.0;
    for (auto& v : filterKey) v = -1.0;
    for (auto& v : detKey) v = -1.0;
    autoKey[0] = autoKey[1] = -1.0e9;
    autoKey[2] = -1.0;
}

BandProcessor::Key BandProcessor::makeKey (const BandParams& p) noexcept
{
    Key k;
    k.active = p.isActive();
    k.shape = (int) p.shape;
    k.slope = p.slope;
    k.type = (int) p.type;
    k.ftype = (int) p.filterType;
    k.fpost = p.filterPost ? 1 : 0;
    k.ntype = (int) p.noiseType;
    k.comp = p.compOn ? 1 : 0;
    k.clip = (int) p.clipMode;
    return k;
}

void BandProcessor::applyKey (const Key& k)
{
    key = k;
    keyCompOn = k.comp != 0;
    // Called while the band is faded out: restart every stateful module.
    region.clear();
    noiseRegion.clear();
    filt.clear();
    dc.reset();
    phat.reset();
    comp.reset();
    clipper.reset();
    noiseEnv = 0.0;
    crushCount = 0;
    crushHeld[0] = crushHeld[1] = 0.0;
    for (auto& v : regionKey) v = -1.0;
    for (auto& v : filterKey) v = -1.0;
    designRegion (true);
    designFilter (true);
}

void BandProcessor::designRegion (bool snap)
{
    const double k[4] { sLogF, sLogQ, (double) key.shape, (double) key.slope };
    if (std::memcmp (k, regionKey, sizeof (k)) == 0) return;
    const bool fresh = regionKey[0] < -0.5 && regionKey[2] < -0.5;
    std::memcpy (regionKey, k, sizeof (k));

    DigitalFilter d;
    if ((Shape) key.shape != Shape::Full)
    {
        DesignParams dp;
        dp.freq = std::exp (sLogF);
        dp.slopeIdx = regionSlopeDesignIdx (key.slope);
        switch ((Shape) key.shape)
        {
            case Shape::Bell: dp.type = FilterType::BandPass; dp.q = std::exp (sLogQ); break;
            case Shape::Low:  dp.type = FilterType::HighCut;  dp.q = kDefaultQ; break;
            case Shape::High: dp.type = FilterType::LowCut;   dp.q = kDefaultQ; break;
            case Shape::Full:
            case Shape::Count:
            default: break;
        }
        d = designDigital (designAnalog (dp, osRate));
    }
    region.setTarget (d, snap || fresh);
    noiseRegion.setTarget (d, snap || fresh);
}

void BandProcessor::designFilter (bool snap)
{
    if ((BandFilter) key.ftype == BandFilter::Off) return;
    const double k[3] { sLogFF, sLogRes, (double) key.ftype };
    if (std::memcmp (k, filterKey, sizeof (k)) == 0) return;
    const bool fresh = filterKey[2] < -0.5;
    std::memcpy (filterKey, k, sizeof (k));

    DesignParams dp;
    dp.freq = std::exp (sLogFF);
    dp.q = std::exp (sLogRes);
    dp.slopeIdx = 1;   // 12 dB/oct
    switch ((BandFilter) key.ftype)
    {
        case BandFilter::LowPass:  dp.type = FilterType::HighCut;  break;
        case BandFilter::HighPass: dp.type = FilterType::LowCut;   break;
        case BandFilter::BandPass: dp.type = FilterType::BandPass; break;
        case BandFilter::Off:
        case BandFilter::Count:
        default: return;
    }
    filt.setTarget (designDigital (designAnalog (dp, osRate)), snap || fresh);
}

void BandProcessor::update (const BandParams& p, int hostCount, const float* detL, const float* detR)
{
    cur = p;
    const Key want = makeKey (p);
    const double alpha = 1.0 - std::exp (-(hostCount / hostRate) / 0.02);

    auto smooth = [alpha] (double& v, double t, double eps)
    {
        v += alpha * (t - v);
        if (std::abs (t - v) < eps) v = t;
    };

    if (! initialised)
    {
        sLogF = std::log (p.freq); sLogQ = std::log (p.q);
        sDrive = p.driveDb; sBias = p.bias; sWet = p.wet; sGain = p.gainDb;
        sLogFF = std::log (p.filterFreq); sLogRes = std::log (p.filterRes);
        sPhat = p.phat; sNoiseDb = p.noiseDb; sFollow = p.noiseFollow; sCeil = p.clipCeiling;
        applyKey (want);
        fade = fadeTarget = want.active ? 1.0 : 0.0;
        initialised = true;
    }
    else
    {
        smooth (sLogF, std::log (p.freq), 1.0e-5);
        smooth (sLogQ, std::log (p.q), 1.0e-5);
        smooth (sDrive, p.driveDb, 1.0e-4);
        smooth (sBias, p.bias, 1.0e-5);
        smooth (sWet, p.wet, 1.0e-5);
        smooth (sGain, p.gainDb, 1.0e-4);
        smooth (sLogFF, std::log (p.filterFreq), 1.0e-5);
        smooth (sLogRes, std::log (p.filterRes), 1.0e-5);
        smooth (sPhat, p.phat, 1.0e-5);
        smooth (sNoiseDb, p.noiseDb, 1.0e-3);
        smooth (sFollow, p.noiseFollow, 1.0e-5);
        smooth (sCeil, p.clipCeiling, 1.0e-4);

        if (! (want == key))
        {
            if (fade <= 0.0)
            {
                applyKey (want);
                fadeTarget = want.active ? 1.0 : 0.0;
            }
            else
            {
                fadeTarget = 0.0;   // fade out first, switch, then fade back in
            }
        }
        else
        {
            fadeTarget = key.active ? 1.0 : 0.0;
        }
    }

    // --- Dynamics -------------------------------------------------------------
    if (key.active && p.dynOn && detL != nullptr)
    {
        const double dk[3] { (double) key.shape, p.freq, p.q };
        if (std::memcmp (dk, detKey, sizeof (dk)) != 0)
        {
            std::memcpy (detKey, dk, sizeof (dk));
            DesignParams dp;
            dp.freq = p.freq;
            dp.q = p.q;
            switch ((Shape) key.shape)
            {
                case Shape::Bell: dp.type = FilterType::Bell;      break;   // band-pass detector
                case Shape::Low:  dp.type = FilterType::LowShelf;  break;   // low-pass detector
                case Shape::High: dp.type = FilterType::HighShelf; break;   // high-pass detector
                case Shape::Full:
                case Shape::Count:
                default:          dp.type = FilterType::FlatTilt;  break;   // broadband
            }
            det.setFilter (designDigital (designDetectorFilter (dp, hostRate)));
        }
        det.setTimes (p.dynAttack, p.dynRelease);
        det.process (detL, detR, hostCount);
        amount = dynamicAmount (det.levelDb(), p.dynThresh);
    }
    else
    {
        amount *= (1.0 - alpha);
        if (amount < 1.0e-4) amount = 0.0;
    }

    // --- Effective values (static + dynamic) -----------------------------------
    const double driveDb = juce::jlimit (0.0, 60.0, sDrive + p.dynDrive * amount);
    const double wet     = juce::jlimit (0.0, 1.0, sWet + p.dynWet * amount);
    const double gainDb  = juce::jlimit (-48.0, 48.0, sGain + p.dynGain * amount);

    designRegion (false);
    designFilter (false);

    const auto type = (DistType) key.type;
    if (p.autoGain && type != DistType::Bitcrush)
    {
        if (std::abs (driveDb - autoKey[0]) > 0.01 || std::abs (sBias - autoKey[1]) > 1.0e-4 || autoKey[2] != (double) key.type)
        {
            autoFactor = autoGainFactor (type, driveDb, sBias);
            autoKey[0] = driveDb; autoKey[1] = sBias; autoKey[2] = (double) key.type;
        }
    }
    else
    {
        autoFactor = 1.0;
        autoKey[2] = -1.0;
    }

    tDrive = dbToLin (driveDb);
    tBias  = sBias;
    tAuto  = autoFactor;
    tWet   = wet;
    tGain  = dbToLin (gainDb);
    tPhat  = sPhat;
    tNoise = sNoiseDb > kNoiseOffDb + 0.05 ? dbToLin (sNoiseDb) : 0.0;
    crushBitsNow = crushBits (driveDb);
    crushHoldNow = crushHold (sBias) * osFactor;

    if (keyCompOn)
        comp.set (p.compThresh, p.compRatio, p.compAttack, p.compRelease, p.compMakeup);
}

void BandProcessor::process (double* L, double* R, int n, bool stereo, BandScratch& s, double* soloL, double* soloR) noexcept
{
    auto finish = [this]
    {
        rDrive = tDrive; rBias = tBias; rAuto = tAuto; rWet = tWet; rGain = tGain; rPhat = tPhat; rNoise = tNoise;
    };

    if (! audible() || n <= 0)
    {
        finish();
        return;
    }

    n = std::min (n, BandScratch::kMax);
    const double inv = 1.0 / (double) n;
    const int nch = stereo ? 2 : 1;
    double* b[2] { s.bL, s.bR };
    double* x[2] { s.sL, s.sR };
    double* io[2] { L, R };

    // 1. Region
    for (int c = 0; c < nch; ++c)
    {
        std::memcpy (b[c], io[c], sizeof (double) * (size_t) n);
        region.process (c, b[c], n);
        std::memcpy (x[c], b[c], sizeof (double) * (size_t) n);
    }
    region.commit();

    const bool hasFilter = (BandFilter) key.ftype != BandFilter::Off;

    // 2. Pre filter
    if (hasFilter && key.fpost == 0)
        for (int c = 0; c < nch; ++c) filt.process (c, x[c], n);

    // 3. Distortion
    const auto type = (DistType) key.type;
    if (type == DistType::Bitcrush)
    {
        const int hold = std::max (1, crushHoldNow);
        for (int i = 0; i < n; ++i)
        {
            if (crushCount == 0)
                for (int c = 0; c < nch; ++c) crushHeld[c] = crushQuantise (x[c][i], crushBitsNow);
            for (int c = 0; c < nch; ++c) x[c][i] = crushHeld[c];
            if (++crushCount >= hold) crushCount = 0;
        }
    }
    else
    {
        const double dd = (tDrive - rDrive) * inv, db = (tBias - rBias) * inv, da = (tAuto - rAuto) * inv;
        for (int c = 0; c < nch; ++c)
        {
            double* v = x[c];
            for (int i = 0; i < n; ++i)
            {
                const double k = (double) (i + 1);
                v[i] = shapeBiased (type, v[i] * (rDrive + dd * k), rBias + db * k) * (rAuto + da * k);
            }
        }
    }
    for (int c = 0; c < nch; ++c) dc.process (c, x[c], n);

    // 4. Post filter
    if (hasFilter && key.fpost == 1)
        for (int c = 0; c < nch; ++c) filt.process (c, x[c], n);
    if (hasFilter) filt.commit();

    // 5. Phat
    if (rPhat > 0.0 || tPhat > 0.0)
    {
        const double dp = (tPhat - rPhat) * inv;
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
                x[c][i] += (rPhat + dp * (double) (i + 1)) * phat.process (c, x[c][i]);
    }

    // 6. Noise (shaped by the band region, optionally following the band level)
    if (rNoise > 0.0 || tNoise > 0.0)
    {
        const double dn = (tNoise - rNoise) * inv;
        const double follow = sFollow;
        for (int i = 0; i < n; ++i)
        {
            const double pk = std::max (std::abs (b[0][i]), stereo ? std::abs (b[1][i]) : 0.0);
            noiseEnv = pk > noiseEnv ? pk : pk + noiseEnvRel * (noiseEnv - pk);
            s.n1[i] = (rNoise + dn * (double) (i + 1)) * ((1.0 - follow) + follow * std::min (1.0, 2.0 * noiseEnv));
        }
        const auto nt = (NoiseType) key.ntype;
        for (int c = 0; c < nch; ++c)
        {
            for (int i = 0; i < n; ++i) s.n2[i] = noise[c].next (nt) * s.n1[i];
            noiseRegion.process (c, s.n2, n);
            for (int i = 0; i < n; ++i) x[c][i] += s.n2[i];
        }
        if (! std::isfinite (noiseEnv)) noiseEnv = 0.0;
    }
    noiseRegion.commit();

    // 7. Compressor
    if (keyCompOn)
        comp.process (x[0], x[1], n, stereo);

    // 8. Clipper / limiter
    if ((ClipMode) key.clip != ClipMode::Off)
        clipper.process ((ClipMode) key.clip, sCeil, x[0], x[1], n, stereo);

    // 9. Wet / gain and put the band back into the signal
    const double dw = (tWet - rWet) * inv, dg = (tGain - rGain) * inv;
    double f = fade;
    for (int i = 0; i < n; ++i)
    {
        if (f < fadeTarget)      f = std::min (fadeTarget, f + fadeStep);
        else if (f > fadeTarget) f = std::max (fadeTarget, f - fadeStep);
        const double k = (double) (i + 1);
        const double w = rWet + dw * k, g = rGain + dg * k;
        for (int c = 0; c < nch; ++c)
        {
            const double out = g * (b[c][i] + w * (x[c][i] - b[c][i]));
            io[c][i] += f * (out - b[c][i]);
            if (c == 0 && soloL != nullptr) soloL[i] = out;
            if (c == 1 && soloR != nullptr) soloR[i] = out;
        }
    }
    fade = f;
    finish();
}

} // namespace gkd::dsp
