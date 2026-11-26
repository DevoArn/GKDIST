// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/FilterTypes.h"
#include <array>
#include <atomic>

namespace gkd
{

constexpr int kNumBands = 24;

/** Which part of the spectrum a band takes from the signal. */
enum class Shape : int { Bell = 0, Low, High, Full, Count };
enum class DistType : int { SoftClip = 0, HardClip, Tube, Tape, Transistor, Diode, Fuzz, Foldback, Rectify, Bitcrush, Count };
enum class BandFilter : int { Off = 0, LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf, Tilt, Count };
enum class FxType : int { Off = 0, Phaser, Flanger, Chorus, Vibrato, Tremolo, AutoPan, Count };
enum class DetMode : int { Region = 0, Bell, Count };

/** Modules of a band's effect chain; their order is a per-band parameter. */
enum class Module : int { Filter = 0, Distortion, Phat, Noise, Fx, Compressor, Clipper, Count };
constexpr int kNumModules = (int) Module::Count;
constexpr int kNumChainOrders = 5040;   // 7!
/** Chain order <-> code (Lehmer code; the default order Filter, Distortion, ... is 0). */
void decodeChainOrder (int code, int order[kNumModules]) noexcept;
int  encodeChainOrder (const int order[kNumModules]) noexcept;
juce::StringArray moduleNames();        // "FILTER", "DRIVE", ...
inline bool filterHasGain (BandFilter f) noexcept
{
    return f == BandFilter::Peak || f == BandFilter::LowShelf || f == BandFilter::HighShelf || f == BandFilter::Tilt;
}
/** Filter shape used to design a band filter (LowPass = high cut etc.). */
inline FilterType bandFilterDesignType (BandFilter f) noexcept
{
    switch (f)
    {
        case BandFilter::LowPass:   return FilterType::HighCut;
        case BandFilter::HighPass:  return FilterType::LowCut;
        case BandFilter::BandPass:  return FilterType::BandPass;
        case BandFilter::Notch:     return FilterType::Notch;
        case BandFilter::Peak:      return FilterType::Bell;
        case BandFilter::LowShelf:  return FilterType::LowShelf;
        case BandFilter::HighShelf: return FilterType::HighShelf;
        case BandFilter::Tilt:      return FilterType::TiltShelf;
        case BandFilter::Off:
        case BandFilter::Count:
        default:                    return FilterType::AllPass;
    }
}

// --- Modulation ---------------------------------------------------------------
constexpr int kNumModSlots = 8;
enum class ModSource : int { Off = 0, Lfo1, Lfo2, InputEnv, SidechainEnv, Count };
enum class ModTarget : int { Drive = 0, Wet, Gain, Frequency, Q, Bias, FilterFreq, FilterReso, FilterGain, Phat, Noise, FxRate, FxDepth, FxMix, Pan, Count };
enum class LfoShape : int { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Count };
constexpr int kNumLfoDivs = 11;
/** Length of an LFO cycle in quarter notes for a tempo-sync division. */
inline double lfoDivBeats (int idx) noexcept
{
    static constexpr double b[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return b[juce::jlimit (0, kNumLfoDivs - 1, idx)];
}
enum class NoiseType : int { White = 0, Pink, Crackle, Count };
enum class ClipMode : int { Off = 0, Soft, Hard, Limit, Count };

constexpr int kNumRegionSlopes = 3;   // 12, 24, 48 dB/oct
inline int regionSlopeDb (int idx) noexcept { static constexpr int s[] = { 12, 24, 48 }; return s[juce::jlimit (0, 2, idx)]; }
/** Slope index in FilterDesign's table (6,12,18,24,30,36,48,...). */
inline int regionSlopeDesignIdx (int idx) noexcept { static constexpr int s[] = { 1, 3, 6 }; return s[juce::jlimit (0, 2, idx)]; }

juce::StringArray shapeNames();
juce::StringArray regionSlopeNames();
juce::StringArray distTypeNames();
juce::StringArray bandFilterNames();
juce::StringArray filterSlopeNames();
juce::StringArray fxTypeNames();
juce::StringArray detModeNames();
juce::StringArray modSourceNames();
juce::StringArray modTargetNames();
juce::StringArray modBandNames();       // "All Bands", "Band 1" ... "Band 24"
juce::StringArray lfoShapeNames();
juce::StringArray lfoDivNames();
juce::StringArray noiseTypeNames();
juce::StringArray clipModeNames();
juce::StringArray oversamplingNames();
juce::StringArray globalSlopeNames();

namespace ID
{
    juce::String band (int bandIndex, const char* suffix);
    juce::String modSlot (int slot, const char* suffix);

    // band
    inline constexpr const char* used = "used";
    inline constexpr const char* enabled = "on";
    inline constexpr const char* solo = "solo";
    inline constexpr const char* shape = "shape";
    inline constexpr const char* freq = "freq";
    inline constexpr const char* q = "q";
    inline constexpr const char* slope = "slope";
    inline constexpr const char* type = "type";
    inline constexpr const char* drive = "drive";
    inline constexpr const char* bias = "bias";
    inline constexpr const char* autoGain = "autogain";
    inline constexpr const char* filterType = "ftype";
    inline constexpr const char* filterSlope = "fslope";
    inline constexpr const char* filterGain = "fgain";
    inline constexpr const char* fxType = "fx";
    inline constexpr const char* fxRate = "fxrate";
    inline constexpr const char* fxDepth = "fxdepth";
    inline constexpr const char* fxFeedback = "fxfb";
    inline constexpr const char* fxMix = "fxmix";
    inline constexpr const char* chainOrder = "order";
    inline constexpr const char* pan = "pan";
    inline constexpr const char* mute = "mute";
    inline constexpr const char* detMode = "detmode";
    inline constexpr const char* detFreq = "detfreq";
    inline constexpr const char* detQ = "detq";
    inline constexpr const char* filterFreq = "ffreq";
    inline constexpr const char* filterRes = "fres";
    inline constexpr const char* phat = "phat";
    inline constexpr const char* noiseLevel = "noise";
    inline constexpr const char* noiseType = "ntype";
    inline constexpr const char* noiseFollow = "nfollow";
    inline constexpr const char* compOn = "comp";
    inline constexpr const char* compThresh = "cthresh";
    inline constexpr const char* compRatio = "cratio";
    inline constexpr const char* compAttack = "catk";
    inline constexpr const char* compRelease = "crel";
    inline constexpr const char* compMakeup = "cmakeup";
    inline constexpr const char* clipMode = "clip";
    inline constexpr const char* clipCeiling = "ceiling";
    inline constexpr const char* wet = "wet";
    inline constexpr const char* gain = "gain";
    inline constexpr const char* dynOn = "dyn";
    inline constexpr const char* dynSC = "sc";
    inline constexpr const char* dynThresh = "dthresh";
    inline constexpr const char* dynAttack = "datk";
    inline constexpr const char* dynRelease = "drel";
    inline constexpr const char* dynGain = "dgain";
    inline constexpr const char* dynDrive = "ddrive";
    inline constexpr const char* dynWet = "dwet";

    // global
    inline constexpr const char* oversampling = "os";
    inline constexpr const char* hpOn = "g_hp";
    inline constexpr const char* hpFreq = "g_hpfreq";
    inline constexpr const char* lpOn = "g_lp";
    inline constexpr const char* lpFreq = "g_lpfreq";
    inline constexpr const char* filtSlope = "g_fslope";
    inline constexpr const char* gCompOn = "g_comp";
    inline constexpr const char* gCompThresh = "g_cthresh";
    inline constexpr const char* gCompRatio = "g_cratio";
    inline constexpr const char* gCompAttack = "g_catk";
    inline constexpr const char* gCompRelease = "g_crel";
    inline constexpr const char* gCompMakeup = "g_cmakeup";
    inline constexpr const char* gNoiseLevel = "g_noise";
    inline constexpr const char* gNoiseType = "g_ntype";
    inline constexpr const char* gClipOn = "g_clip";
    inline constexpr const char* gClipCeiling = "g_clipceil";
    inline constexpr const char* gClipSoft = "g_clipsoft";
    inline constexpr const char* limitOn = "g_limit";
    inline constexpr const char* limitCeiling = "g_limceil";
    inline constexpr const char* limitRelease = "g_limrel";
    inline constexpr const char* mix = "g_mix";
    inline constexpr const char* outGain = "g_out";
    inline constexpr const char* bypass = "bypass";
    inline constexpr const char* lfo1Rate = "lfo1_rate";
    inline constexpr const char* lfo1Shape = "lfo1_shape";
    inline constexpr const char* lfo1Sync = "lfo1_sync";
    inline constexpr const char* lfo1Div = "lfo1_div";
    inline constexpr const char* lfo2Rate = "lfo2_rate";
    inline constexpr const char* lfo2Shape = "lfo2_shape";
    inline constexpr const char* lfo2Sync = "lfo2_sync";
    inline constexpr const char* lfo2Div = "lfo2_div";
    inline constexpr const char* envAttack = "env_atk";
    inline constexpr const char* envRelease = "env_rel";

    // modulation slots ("m1_src" ...)
    inline constexpr const char* modSource = "src";
    inline constexpr const char* modBand = "band";
    inline constexpr const char* modTarget = "dst";
    inline constexpr const char* modAmount = "amt";
}

constexpr float kNoiseOffDb = -80.0f;   // noise level at the bottom of its range = off

/** Order of the per-band and global parameter tables (Params.cpp). */
enum BandParamIndex
{
    bpUsed, bpEnabled, bpSolo, bpShape, bpFreq, bpQ, bpSlope, bpType, bpDrive, bpBias, bpAutoGain,
    bpFilterType, bpFilterSlope, bpFilterFreq, bpFilterRes, bpFilterGain, bpPhat, bpNoiseLevel, bpNoiseType, bpNoiseFollow,
    bpCompOn, bpCompThresh, bpCompRatio, bpCompAttack, bpCompRelease, bpCompMakeup, bpClipMode, bpClipCeiling,
    bpWet, bpGain, bpDynOn, bpDynSC, bpDynThresh, bpDynAttack, bpDynRelease, bpDynGain, bpDynDrive, bpDynWet,
    bpFxType, bpFxRate, bpFxDepth, bpFxFeedback, bpFxMix, bpChainOrder, bpPan, bpMute, bpDetMode, bpDetFreq, bpDetQ,
    kNumBandParams
};

enum GlobalParamIndex
{
    gpOversampling, gpHpOn, gpHpFreq, gpLpOn, gpLpFreq, gpFiltSlope, gpCompOn, gpCompThresh, gpCompRatio,
    gpCompAttack, gpCompRelease, gpCompMakeup, gpNoiseLevel, gpNoiseType, gpClipOn, gpClipCeiling, gpClipSoft,
    gpLimitOn, gpLimitCeiling, gpLimitRelease, gpMix, gpOutGain, gpBypass,
    gpLfo1Rate, gpLfo1Shape, gpLfo1Sync, gpLfo1Div, gpLfo2Rate, gpLfo2Shape, gpLfo2Sync, gpLfo2Div,
    gpEnvAttack, gpEnvRelease,
    kNumGlobalParams
};

enum ModSlotParamIndex { mpSource, mpBand, mpTarget, mpAmount, kNumModSlotParams };

/** Parameter ID of a band parameter by table index. */
juce::String bandParamId (int band, int index);
/** Parameter ID of a global parameter by table index. */
juce::String globalParamId (int index);
/** Parameter ID of a modulation-slot parameter. */
juce::String modParamId (int slot, int index);

struct BandParams
{
    bool       used = false, enabled = true, solo = false;
    Shape      shape = Shape::Bell;
    double     freq = 1000.0, q = 1.0;
    int        slope = 0;
    DistType   type = DistType::Tube;
    double     driveDb = 12.0, bias = 0.0;
    bool       autoGain = true;
    BandFilter filterType = BandFilter::Off;
    int        filterSlope = 0;            // 12 / 24 / 48 dB/oct
    double     filterFreq = 5000.0, filterRes = kDefaultQ, filterGainDb = 0.0;
    double     phat = 0.0;                 // 0..1
    double     noiseDb = kNoiseOffDb;
    NoiseType  noiseType = NoiseType::White;
    double     noiseFollow = 0.0;          // 0..1
    bool       compOn = false;
    double     compThresh = -18.0, compRatio = 4.0, compAttack = 10.0, compRelease = 120.0, compMakeup = 0.0;
    ClipMode   clipMode = ClipMode::Off;
    double     clipCeiling = 0.0;
    double     wet = 1.0;                  // 0..1
    double     gainDb = 0.0;
    bool       dynOn = false, dynSC = false;
    double     dynThresh = -30.0, dynAttack = 10.0, dynRelease = 150.0;
    double     dynGain = 0.0, dynDrive = 0.0, dynWet = 0.0;   // dB, dB, -1..1
    FxType     fxType = FxType::Off;
    double     fxRate = 0.5, fxDepth = 0.5, fxFeedback = 0.0, fxMix = 0.5;   // Hz, 0..1, -0.95..0.95, 0..1
    int        chainCode = 0;
    int        order[kNumModules] { 0, 1, 2, 3, 4, 5, 6 };
    double     pan = 0.0;                  // -1..1
    bool       mute = false;
    DetMode    detMode = DetMode::Bell;
    double     detFreq = 1000.0, detQ = 1.0;

    bool isActive() const noexcept { return used && enabled; }
};

struct GlobalParams
{
    int    osLog2 = 2;
    bool   hpOn = false, lpOn = false;
    double hpFreq = 30.0, lpFreq = 18000.0;
    int    filtSlope = 0;                  // 0 = 12, 1 = 24 dB/oct
    bool   compOn = false;
    double compThresh = -12.0, compRatio = 2.0, compAttack = 10.0, compRelease = 150.0, compMakeup = 0.0;
    double noiseDb = kNoiseOffDb;
    NoiseType noiseType = NoiseType::White;
    bool   clipOn = false, clipSoft = true;
    double clipCeiling = -0.3;
    bool   limitOn = false;
    double limitCeiling = -0.3, limitRelease = 80.0;
    double mix = 1.0;                      // 0..1
    double outGainDb = 0.0;
    bool   bypass = false;
    double lfoRate[2] { 1.0, 0.25 };       // Hz
    LfoShape lfoShape[2] { LfoShape::Sine, LfoShape::Triangle };
    bool   lfoSync[2] { false, false };
    int    lfoDiv[2] { 6, 8 };
    double envAttack = 10.0, envRelease = 150.0;   // ms
};

struct ModSlot
{
    ModSource source = ModSource::Off;
    int       band = -1;                   // -1 = all bands
    ModTarget target = ModTarget::Drive;
    double    amount = 0.0;                // -1..1
};

/** Modulation offsets for one band (added to the band's smoothed values). */
struct BandMod
{
    double driveDb = 0, wet = 0, gainDb = 0, freqOct = 0, qOct = 0, bias = 0, filterOct = 0, resoOct = 0,
           filterGainDb = 0, phat = 0, noiseDb = 0, fxRateOct = 0, fxDepth = 0, fxMix = 0, pan = 0;

    /** Adds a modulation of `value` (-1..1, source x amount) to a target. */
    void add (ModTarget t, double value) noexcept
    {
        switch (t)
        {
            case ModTarget::Drive:      driveDb += 48.0 * value; break;
            case ModTarget::Wet:        wet += value; break;
            case ModTarget::Gain:       gainDb += 24.0 * value; break;
            case ModTarget::Frequency:  freqOct += 4.0 * value; break;
            case ModTarget::Q:          qOct += 3.0 * value; break;
            case ModTarget::Bias:       bias += value; break;
            case ModTarget::FilterFreq: filterOct += 4.0 * value; break;
            case ModTarget::FilterReso: resoOct += 3.0 * value; break;
            case ModTarget::FilterGain: filterGainDb += 24.0 * value; break;
            case ModTarget::Phat:       phat += value; break;
            case ModTarget::Noise:      noiseDb += 60.0 * value; break;
            case ModTarget::FxRate:     fxRateOct += 3.0 * value; break;
            case ModTarget::FxDepth:    fxDepth += value; break;
            case ModTarget::FxMix:      fxMix += value; break;
            case ModTarget::Pan:        pan += value; break;
            case ModTarget::Count:
            default: break;
        }
    }
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw parameter pointers for lock-free reads on any thread. */
class ParamRefs
{
public:
    void init (juce::AudioProcessorValueTreeState& state);
    BandParams readBand (int b) const noexcept;
    GlobalParams readGlobal() const noexcept;
    ModSlot readModSlot (int slot) const noexcept;

    struct Band
    {
        std::atomic<float>* p[kNumBandParams] {};
    };
    std::array<Band, kNumBands> bands;
    std::atomic<float>* g[kNumGlobalParams] {};
    std::atomic<float>* m[kNumModSlots][kNumModSlotParams] {};
};

/** Number with a fixed count of decimals. (juce::String (x, 0) does NOT round:
    0 decimal places means "as many as needed" in JUCE.) */
juce::String formatNumber (double v, int decimals);
juce::String formatFrequency (double hz);
juce::String formatDb (double db, bool sign = true);
juce::String noteNameForFrequency (double hz);

} // namespace gkd
