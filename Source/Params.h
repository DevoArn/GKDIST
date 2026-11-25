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
enum class BandFilter : int { Off = 0, LowPass, HighPass, BandPass, Count };
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
juce::StringArray filterPosNames();
juce::StringArray noiseTypeNames();
juce::StringArray clipModeNames();
juce::StringArray oversamplingNames();
juce::StringArray globalSlopeNames();

namespace ID
{
    juce::String band (int bandIndex, const char* suffix);

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
    inline constexpr const char* filterPos = "fpos";
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
}

constexpr float kNoiseOffDb = -80.0f;   // noise level at the bottom of its range = off

/** Order of the per-band and global parameter tables (Params.cpp). */
enum BandParamIndex
{
    bpUsed, bpEnabled, bpSolo, bpShape, bpFreq, bpQ, bpSlope, bpType, bpDrive, bpBias, bpAutoGain,
    bpFilterType, bpFilterPos, bpFilterFreq, bpFilterRes, bpPhat, bpNoiseLevel, bpNoiseType, bpNoiseFollow,
    bpCompOn, bpCompThresh, bpCompRatio, bpCompAttack, bpCompRelease, bpCompMakeup, bpClipMode, bpClipCeiling,
    bpWet, bpGain, bpDynOn, bpDynSC, bpDynThresh, bpDynAttack, bpDynRelease, bpDynGain, bpDynDrive, bpDynWet,
    kNumBandParams
};

enum GlobalParamIndex
{
    gpOversampling, gpHpOn, gpHpFreq, gpLpOn, gpLpFreq, gpFiltSlope, gpCompOn, gpCompThresh, gpCompRatio,
    gpCompAttack, gpCompRelease, gpCompMakeup, gpNoiseLevel, gpNoiseType, gpClipOn, gpClipCeiling, gpClipSoft,
    gpLimitOn, gpLimitCeiling, gpLimitRelease, gpMix, gpOutGain, gpBypass,
    kNumGlobalParams
};

/** Parameter ID of a band parameter by table index. */
juce::String bandParamId (int band, int index);
/** Parameter ID of a global parameter by table index. */
juce::String globalParamId (int index);

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
    bool       filterPost = false;
    double     filterFreq = 5000.0, filterRes = kDefaultQ;
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
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw parameter pointers for lock-free reads on any thread. */
class ParamRefs
{
public:
    void init (juce::AudioProcessorValueTreeState& state);
    BandParams readBand (int b) const noexcept;
    GlobalParams readGlobal() const noexcept;

    struct Band
    {
        std::atomic<float>* p[kNumBandParams] {};
    };
    std::array<Band, kNumBands> bands;
    std::atomic<float>* g[kNumGlobalParams] {};
};

juce::String formatFrequency (double hz);
juce::String formatDb (double db, bool sign = true);
juce::String noteNameForFrequency (double hz);

} // namespace gkd
