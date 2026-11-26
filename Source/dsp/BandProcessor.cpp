// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BandProcessor.h"
#include <cstring>

namespace gkd::dsp
{

namespace
{
    constexpr double kLn2 = 0.69314718055994530942;
}

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
    fx.prepare (osRate, 0x9e3779b9u * (uint32_t) (bandIndex + 1));
    comp.prepare (osRate);
    clipper.prepare (osRate);
    det.prepare (hostRate);
    noiseEnvRel = std::exp (-1.0 / (0.05 * osRate));
    fadeStep = 1.0 / (0.005 * osRate);              // 5 ms cross-fades
    levelDecay = std::exp (-1.0 / (0.3 * osRate));  // 300 ms meter decay
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
    fx.reset();
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
    levelEnv = 0.0;
    detLevelDb = -100.0;
}

BandProcessor::Key BandProcessor::makeKey (const BandParams& p) noexcept
{
    Key k;
    k.active = p.isActive();
    k.shape = (int) p.shape;
    k.slope = p.slope;
    k.type = (int) p.type;
    k.ftype = (int) p.filterType;
    k.fslope = p.filterSlope;
    k.ntype = (int) p.noiseType;
    k.comp = p.compOn ? 1 : 0;
    k.clip = (int) p.clipMode;
    k.fx = (int) p.fxType;
    k.order = p.chainCode;
    return k;
}

void BandProcessor::applyKey (const Key& k)
{
    key = k;
    keyCompOn = k.comp != 0;
    decodeChainOrder (k.order, order);
    // Called while the band is faded out: restart every stateful module.
    region.clear();
    noiseRegion.clear();
    filt.clear();
    dc.reset();
    phat.reset();
    fx.reset();
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
    const double k[4] { eLogF, eLogQ, (double) key.shape, (double) key.slope };
    if (std::memcmp (k, regionKey, sizeof (k)) == 0) return;
    const bool fresh = regionKey[0] < -0.5 && regionKey[2] < -0.5;
    std::memcpy (regionKey, k, sizeof (k));

    DigitalFilter d;
    if ((Shape) key.shape != Shape::Full)
    {
        DesignParams dp;
        dp.freq = std::exp (eLogF);
        dp.slopeIdx = regionSlopeDesignIdx (key.slope);
        switch ((Shape) key.shape)
        {
            case Shape::Bell: dp.type = FilterType::BandPass; dp.q = std::exp (eLogQ); break;
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
    const auto ft = (BandFilter) key.ftype;
    if (ft == BandFilter::Off) return;
    const double k[5] { eLogFF, eLogRes, eFGain, (double) key.ftype, (double) key.fslope };
    if (std::memcmp (k, filterKey, sizeof (k)) == 0) return;
    const bool fresh = filterKey[3] < -0.5;
    std::memcpy (filterKey, k, sizeof (k));

    DesignParams dp;
    dp.freq = std::exp (eLogFF);
    dp.q = std::exp (eLogRes);
    dp.gainDb = eFGain;
    dp.slopeIdx = regionSlopeDesignIdx (key.fslope);
    dp.type = bandFilterDesignType (ft);
    filt.setTarget (designDigital (designAnalog (dp, osRate)), snap || fresh);
}

void BandProcessor::update (const BandParams& p, const BandMod& mod, int hostCount, const float* detL, const float* detR)
{
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
        sLogFF = std::log (p.filterFreq); sLogRes = std::log (p.filterRes); sFGain = p.filterGainDb;
        sPhat = p.phat; sNoiseDb = p.noiseDb; sFollow = p.noiseFollow; sCeil = p.clipCeiling;
        sPan = p.pan; sMute = p.mute ? 0.0 : 1.0;
        sLogFxRate = std::log (p.fxRate); sFxDepth = p.fxDepth; sFxFb = p.fxFeedback; sFxMix = p.fxMix;
        eLogF = sLogF; eLogQ = sLogQ; eLogFF = sLogFF; eLogRes = sLogRes; eFGain = sFGain;
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
        smooth (sFGain, p.filterGainDb, 1.0e-4);
        smooth (sPhat, p.phat, 1.0e-5);
        smooth (sNoiseDb, p.noiseDb, 1.0e-3);
        smooth (sFollow, p.noiseFollow, 1.0e-5);
        smooth (sCeil, p.clipCeiling, 1.0e-4);
        smooth (sPan, p.pan, 1.0e-5);
        smooth (sMute, p.mute ? 0.0 : 1.0, 1.0e-5);
        smooth (sLogFxRate, std::log (p.fxRate), 1.0e-5);
        smooth (sFxDepth, p.fxDepth, 1.0e-5);
        smooth (sFxFb, p.fxFeedback, 1.0e-5);
        smooth (sFxMix, p.fxMix, 1.0e-5);

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

    // --- Dynamics: own detector bell or the band's region -----------------------
    if (key.active && p.dynOn && detL != nullptr)
    {
        const bool ownBell = p.detMode == DetMode::Bell;
        const double dk[4] { ownBell ? -2.0 : (double) key.shape, ownBell ? p.detFreq : p.freq, ownBell ? p.detQ : p.q, hostRate };
        if (std::memcmp (dk, detKey, sizeof (dk)) != 0)
        {
            std::memcpy (detKey, dk, sizeof (dk));
            DesignParams dp;
            dp.freq = dk[1];
            dp.q = dk[2];
            if (ownBell)
                dp.type = FilterType::Bell;              // band-pass detector at the bell
            else
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
        detLevelDb = det.levelDb();
        amount = dynamicAmount (detLevelDb, p.dynThresh);
    }
    else
    {
        amount *= (1.0 - alpha);
        if (amount < 1.0e-4) amount = 0.0;
        detLevelDb = -100.0;
    }

    // --- Effective values (static + dynamic + modulation) ------------------------
    const double driveDb = juce::jlimit (0.0, 60.0, sDrive + p.dynDrive * amount + mod.driveDb);
    const double wet     = juce::jlimit (0.0, 1.0, sWet + p.dynWet * amount + mod.wet);
    const double gainDb  = juce::jlimit (-48.0, 48.0, sGain + p.dynGain * amount + mod.gainDb);
    const double bias    = juce::jlimit (-1.0, 1.0, sBias + mod.bias);
    const double pan     = juce::jlimit (-1.0, 1.0, sPan + mod.pan);
    const double phatAmt = juce::jlimit (0.0, 1.0, sPhat + mod.phat);
    const double noiseDb = juce::jlimit ((double) kNoiseOffDb, 0.0, sNoiseDb + mod.noiseDb);
    effDriveDb = driveDb; effWet = wet; effGainDb = gainDb;

    const double maxF = std::min (30000.0, 0.49 * osRate);
    eLogF   = juce::jlimit (std::log (10.0), std::log (maxF), sLogF + mod.freqOct * kLn2);
    eLogQ   = juce::jlimit (std::log (0.1), std::log (20.0), sLogQ + mod.qOct * kLn2);
    eLogFF  = juce::jlimit (std::log (20.0), std::log (std::min (20000.0, maxF)), sLogFF + mod.filterOct * kLn2);
    eLogRes = juce::jlimit (std::log (0.1), std::log (10.0), sLogRes + mod.resoOct * kLn2);
    eFGain  = juce::jlimit (-24.0, 24.0, sFGain + mod.filterGainDb);
    eFxRate  = juce::jlimit (0.005, 40.0, std::exp (sLogFxRate + mod.fxRateOct * kLn2));
    eFxDepth = juce::jlimit (0.0, 1.0, sFxDepth + mod.fxDepth);
    eFxMix   = juce::jlimit (0.0, 1.0, sFxMix + mod.fxMix);

    designRegion (false);
    designFilter (false);

    const auto type = (DistType) key.type;
    if (p.autoGain && type != DistType::Bitcrush)
    {
        if (std::abs (driveDb - autoKey[0]) > 0.01 || std::abs (bias - autoKey[1]) > 1.0e-4 || autoKey[2] != (double) key.type)
        {
            autoFactor = autoGainFactor (type, driveDb, bias);
            autoKey[0] = driveDb; autoKey[1] = bias; autoKey[2] = (double) key.type;
        }
    }
    else
    {
        autoFactor = 1.0;
        autoKey[2] = -1.0;
    }

    tDrive = dbToLin (driveDb);
    tBias  = bias;
    tAuto  = autoFactor;
    tWet   = wet;
    tGain  = dbToLin (gainDb) * juce::jlimit (0.0, 1.0, sMute);
    tPhat  = phatAmt;
    tNoise = noiseDb > kNoiseOffDb + 0.05 ? dbToLin (noiseDb) : 0.0;
    tPanL  = pan > 0.0 ? 1.0 - pan : 1.0;
    tPanR  = pan < 0.0 ? 1.0 + pan : 1.0;
    crushBitsNow = crushBits (driveDb);
    crushHoldNow = crushHold (bias) * osFactor;

    if (keyCompOn)
        comp.set (p.compThresh, p.compRatio, p.compAttack, p.compRelease, p.compMakeup);
}

BandTelemetry BandProcessor::telemetry() const noexcept
{
    BandTelemetry t;
    t.dyn = (float) amount;
    t.compGr = compGainReduction();
    t.driveDb = (float) effDriveDb;
    t.wet = (float) effWet;
    t.gainDb = (float) effGainDb;
    t.freq = (float) std::exp (eLogF);
    t.q = (float) std::exp (eLogQ);
    t.levelDb = (float) linToDb (levelEnv);
    t.detDb = (float) detLevelDb;
    return t;
}

void BandProcessor::process (double* L, double* R, int n, bool stereo, BandScratch& s, double* soloL, double* soloR) noexcept
{
    auto finish = [this]
    {
        rDrive = tDrive; rBias = tBias; rAuto = tAuto; rWet = tWet; rGain = tGain; rPhat = tPhat; rNoise = tNoise;
        rPanL = tPanL; rPanR = tPanR;
    };

    if (! audible() || n <= 0)
    {
        levelEnv *= std::pow (levelDecay, (double) std::max (1, n));
        finish();
        return;
    }

    n = std::min (n, BandScratch::kMax);
    const double inv = 1.0 / (double) n;
    const int nch = stereo ? 2 : 1;
    double* b[2] { s.bL, s.bR };
    double* x[2] { s.sL, s.sR };
    double* io[2] { L, R };

    // Region
    for (int c = 0; c < nch; ++c)
    {
        std::memcpy (b[c], io[c], sizeof (double) * (size_t) n);
        region.process (c, b[c], n);
        std::memcpy (x[c], b[c], sizeof (double) * (size_t) n);
    }
    region.commit();

    const bool hasFilter = (BandFilter) key.ftype != BandFilter::Off;
    bool noiseDone = false;

    // Effect chain, in the band's order
    for (int slot = 0; slot < kNumModules; ++slot)
    {
        switch ((Module) order[slot])
        {
            case Module::Filter:
                if (hasFilter)
                {
                    for (int c = 0; c < nch; ++c) filt.process (c, x[c], n);
                    filt.commit();
                }
                break;

            case Module::Distortion:
            {
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
                break;
            }

            case Module::Phat:
                if (rPhat > 0.0 || tPhat > 0.0)
                {
                    const double dp = (tPhat - rPhat) * inv;
                    for (int c = 0; c < nch; ++c)
                        for (int i = 0; i < n; ++i)
                            x[c][i] += (rPhat + dp * (double) (i + 1)) * phat.process (c, x[c][i]);
                }
                break;

            case Module::Noise:
                // Shaped by the band region, optionally following the band level.
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
                noiseDone = true;
                break;

            case Module::Fx:
                fx.process ((FxType) key.fx, eFxRate, eFxDepth, sFxFb, eFxMix, x, nch, n);
                break;

            case Module::Compressor:
                if (keyCompOn)
                    comp.process (x[0], x[1], n, stereo);
                break;

            case Module::Clipper:
                if ((ClipMode) key.clip != ClipMode::Off)
                    clipper.process ((ClipMode) key.clip, sCeil, x[0], x[1], n, stereo);
                break;

            case Module::Count:
            default:
                break;
        }
    }
    if (! noiseDone) noiseRegion.commit();

    // Wet / gain / pan and put the band back into the signal
    const double dw = (tWet - rWet) * inv, dg = (tGain - rGain) * inv;
    const double dpl = (tPanL - rPanL) * inv, dpr = (tPanR - rPanR) * inv;
    double f = fade, pk = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (f < fadeTarget)      f = std::min (fadeTarget, f + fadeStep);
        else if (f > fadeTarget) f = std::max (fadeTarget, f - fadeStep);
        const double k = (double) (i + 1);
        const double w = rWet + dw * k, g = rGain + dg * k;
        for (int c = 0; c < nch; ++c)
        {
            const double pg = ! stereo ? 1.0 : (c == 0 ? rPanL + dpl * k : rPanR + dpr * k);
            const double out = pg * g * (b[c][i] + w * (x[c][i] - b[c][i]));
            io[c][i] += f * (out - b[c][i]);
            pk = std::max (pk, std::abs (out) * f);
            if (c == 0 && soloL != nullptr) soloL[i] = out;
            if (c == 1 && soloR != nullptr) soloR[i] = out;
        }
    }
    fade = f;
    const double decayed = levelEnv * std::pow (levelDecay, (double) n);
    levelEnv = std::isfinite (pk) ? std::max (pk, decayed) : 0.0;
    finish();
}

} // namespace gkd::dsp
