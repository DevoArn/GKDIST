// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Params.h"

namespace gkd
{

juce::StringArray shapeNames()        { return { "Bell", "Low", "High", "Full" }; }
juce::StringArray regionSlopeNames()  { return { "12 dB/oct", "24 dB/oct", "48 dB/oct", "Brickwall" }; }
juce::StringArray distTypeNames()     { return { "Soft Clip", "Hard Clip", "Tube", "Tape", "Transistor", "Diode", "Fuzz", "Foldback", "Rectify", "Bitcrush" }; }
juce::StringArray bandFilterNames()   { return { "Off", "Low Pass", "High Pass", "Band Pass", "Notch", "Peak", "Low Shelf", "High Shelf", "Tilt" }; }
juce::StringArray filterSlopeNames()  { return { "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
juce::StringArray fxTypeNames()       { return { "Off", "Phaser", "Flanger", "Chorus", "Vibrato", "Tremolo", "Auto Pan" }; }
juce::StringArray detModeNames()      { return { "Band Region", "Own Bell" }; }
juce::StringArray modSourceNames()    { return { "Off", "LFO 1", "LFO 2", "Input Envelope", "Side-chain Env" }; }
juce::StringArray modTargetNames()
{
    return { "Drive", "Wet", "Gain", "Frequency", "Q", "Bias", "Filter Freq", "Filter Reso", "Filter Gain",
             "Phat", "Noise", "FX Rate", "FX Depth", "FX Mix", "Pan" };
}
juce::StringArray modBandNames()
{
    juce::StringArray s { "All Bands" };
    for (int b = 1; b <= kNumBands; ++b) s.add ("Band " + juce::String (b));
    return s;
}
juce::StringArray lfoShapeNames()     { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random" }; }
juce::StringArray lfoDivNames()       { return { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }; }
juce::StringArray moduleNames()       { return { "FILTER", "DRIVE", "PHAT", "NOISE", "FX", "COMP", "CLIP" }; }

void decodeChainOrder (int code, int order[kNumModules]) noexcept
{
    code = juce::jlimit (0, kNumChainOrders - 1, code);
    int pool[kNumModules];
    for (int i = 0; i < kNumModules; ++i) pool[i] = i;
    int left = kNumModules;
    int fact = 720;   // 6!
    for (int i = 0; i < kNumModules; ++i)
    {
        const int digit = fact > 0 ? code / fact : 0;
        if (fact > 0) code %= fact;
        order[i] = pool[digit];
        for (int k = digit; k < left - 1; ++k) pool[k] = pool[k + 1];
        --left;
        fact = (kNumModules - 1 - i) > 1 ? fact / (kNumModules - 1 - i) : 0;
    }
}

int encodeChainOrder (const int order[kNumModules]) noexcept
{
    bool usedMod[kNumModules] {};
    int code = 0, fact = 720;
    for (int i = 0; i < kNumModules; ++i)
    {
        const int m = juce::jlimit (0, kNumModules - 1, order[i]);
        if (usedMod[m]) return 0;   // not a permutation
        int digit = 0;
        for (int k = 0; k < m; ++k) if (! usedMod[k]) ++digit;
        usedMod[m] = true;
        code += digit * fact;
        fact = (kNumModules - 1 - i) > 1 ? fact / (kNumModules - 1 - i) : 0;
    }
    return code;
}
juce::StringArray noiseTypeNames()    { return { "White", "Pink", "Crackle" }; }
juce::StringArray clipModeNames()     { return { "Off", "Soft Clip", "Hard Clip", "Limit" }; }
juce::StringArray oversamplingNames() { return { "Off", "2x", "4x", "8x" }; }
juce::StringArray globalSlopeNames()  { return { "12 dB/oct", "24 dB/oct" }; }

juce::String ID::band (int bandIndex, const char* suffix)
{
    return "b" + juce::String (bandIndex + 1) + "_" + suffix;
}

juce::String ID::modSlot (int slot, const char* suffix)
{
    return "m" + juce::String (slot + 1) + "_" + suffix;
}

// ---------------------------------------------------------------------------
juce::String formatNumber (double v, int decimals)
{
    if (! std::isfinite (v)) v = 0.0;
    if (decimals <= 0)
    {
        const auto r = (long long) std::llround (v);
        return juce::String (r == 0 ? 0LL : r);   // no "-0"
    }
    return juce::String (v, decimals);
}

juce::String formatFrequency (double hz)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        return formatNumber (k, k >= 10.0 ? 1 : 2) + " kHz";
    }
    return formatNumber (hz, hz >= 100.0 ? 0 : 1) + " Hz";
}

juce::String formatDb (double db, bool sign)
{
    if (std::abs (db) < 0.005) db = 0.0;
    return (sign && db > 0.0 ? "+" : "") + formatNumber (db, 1) + " dB";
}

juce::String noteNameForFrequency (double hz)
{
    if (hz <= 0.0) return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int note = (int) std::round (midi);
    const int cents = (int) std::round ((midi - note) * 100.0);
    juce::String s = juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    if (cents != 0) s << (cents > 0 ? " +" : " ") << cents << "c";
    return s;
}

static double parseNumber (const juce::String& text)
{
    auto t = text.trim().toLowerCase();
    const double mult = t.containsChar ('k') ? 1000.0 : 1.0;
    return t.retainCharacters ("0123456789.-").getDoubleValue() * mult;
}

// ---------------------------------------------------------------------------
namespace
{
    enum class Kind { Bool, Choice, Float, Order };
    enum class Unit { None, Hz, Db, NoiseDb, Percent, Ms, Q, Ratio, Bias, Rate, Pan };

    struct Spec
    {
        const char* id;
        const char* name;
        Kind kind;
        float lo, hi, def;
        bool  logScale;
        Unit  unit;
        juce::StringArray (*choices)();
    };

    juce::StringArray noChoices() { return {}; }

    // Order must match BandParamIndex.
    const Spec kBandSpecs[] = {
        { ID::used,        "Used",              Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::enabled,     "Enabled",           Kind::Bool,   0, 1, 1, false, Unit::None, noChoices },
        { ID::solo,        "Solo",              Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::shape,       "Shape",             Kind::Choice, 0, 0, 0, false, Unit::None, shapeNames },
        { ID::freq,        "Frequency",         Kind::Float,  10.0f, 30000.0f, 1000.0f, true, Unit::Hz, noChoices },
        { ID::q,           "Q",                 Kind::Float,  0.1f, 20.0f, 1.0f, true, Unit::Q, noChoices },
        { ID::slope,       "Slope",             Kind::Choice, 0, 0, 0, false, Unit::None, regionSlopeNames },
        { ID::type,        "Distortion Type",   Kind::Choice, 0, 0, (float) DistType::Tube, false, Unit::None, distTypeNames },
        { ID::drive,       "Drive",             Kind::Float,  0.0f, 48.0f, 12.0f, false, Unit::Db, noChoices },
        { ID::bias,        "Bias",              Kind::Float,  -1.0f, 1.0f, 0.0f, false, Unit::Bias, noChoices },
        { ID::autoGain,    "Auto Gain",         Kind::Bool,   0, 1, 1, false, Unit::None, noChoices },
        { ID::filterType,  "Filter Type",       Kind::Choice, 0, 0, 0, false, Unit::None, bandFilterNames },
        { ID::filterSlope, "Filter Slope",      Kind::Choice, 0, 0, 0, false, Unit::None, filterSlopeNames },
        { ID::filterFreq,  "Filter Frequency",  Kind::Float,  20.0f, 20000.0f, 5000.0f, true, Unit::Hz, noChoices },
        { ID::filterRes,   "Filter Resonance",  Kind::Float,  0.1f, 10.0f, (float) kDefaultQ, true, Unit::Q, noChoices },
        { ID::filterGain,  "Filter Gain",       Kind::Float,  -24.0f, 24.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::phat,        "Phat",              Kind::Float,  0.0f, 100.0f, 0.0f, false, Unit::Percent, noChoices },
        { ID::noiseLevel,  "Noise Level",       Kind::Float,  kNoiseOffDb, 0.0f, kNoiseOffDb, false, Unit::NoiseDb, noChoices },
        { ID::noiseType,   "Noise Type",        Kind::Choice, 0, 0, 0, false, Unit::None, noiseTypeNames },
        { ID::noiseFollow, "Noise Follow",      Kind::Float,  0.0f, 100.0f, 0.0f, false, Unit::Percent, noChoices },
        { ID::compOn,      "Compressor",        Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::compThresh,  "Comp Threshold",    Kind::Float,  -60.0f, 0.0f, -18.0f, false, Unit::Db, noChoices },
        { ID::compRatio,   "Comp Ratio",        Kind::Float,  1.0f, 20.0f, 4.0f, true, Unit::Ratio, noChoices },
        { ID::compAttack,  "Comp Attack",       Kind::Float,  0.1f, 200.0f, 10.0f, true, Unit::Ms, noChoices },
        { ID::compRelease, "Comp Release",      Kind::Float,  5.0f, 2000.0f, 120.0f, true, Unit::Ms, noChoices },
        { ID::compMakeup,  "Comp Makeup",       Kind::Float,  0.0f, 24.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::clipMode,    "Clip Mode",         Kind::Choice, 0, 0, 0, false, Unit::None, clipModeNames },
        { ID::clipCeiling, "Clip Ceiling",      Kind::Float,  -24.0f, 0.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::wet,         "Wet",               Kind::Float,  0.0f, 100.0f, 100.0f, false, Unit::Percent, noChoices },
        { ID::gain,        "Gain",              Kind::Float,  -24.0f, 24.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::dynOn,       "Dynamics",          Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::dynSC,       "Dyn Ext Side-chain",Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::dynThresh,   "Dyn Threshold",     Kind::Float,  -80.0f, 6.0f, -30.0f, false, Unit::Db, noChoices },
        { ID::dynAttack,   "Dyn Attack",        Kind::Float,  0.1f, 200.0f, 10.0f, true, Unit::Ms, noChoices },
        { ID::dynRelease,  "Dyn Release",       Kind::Float,  5.0f, 3000.0f, 150.0f, true, Unit::Ms, noChoices },
        { ID::dynGain,     "Dyn Gain Range",    Kind::Float,  -24.0f, 24.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::dynDrive,    "Dyn Drive Range",   Kind::Float,  -48.0f, 48.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::dynWet,      "Dyn Wet Range",     Kind::Float,  -100.0f, 100.0f, 0.0f, false, Unit::Percent, noChoices },
        { ID::fxType,      "FX Type",           Kind::Choice, 0, 0, 0, false, Unit::None, fxTypeNames },
        { ID::fxRate,      "FX Rate",           Kind::Float,  0.02f, 20.0f, 0.5f, true, Unit::Rate, noChoices },
        { ID::fxDepth,     "FX Depth",          Kind::Float,  0.0f, 100.0f, 50.0f, false, Unit::Percent, noChoices },
        { ID::fxFeedback,  "FX Feedback",       Kind::Float,  -95.0f, 95.0f, 0.0f, false, Unit::Percent, noChoices },
        { ID::fxMix,       "FX Mix",            Kind::Float,  0.0f, 100.0f, 50.0f, false, Unit::Percent, noChoices },
        { ID::chainOrder,  "Chain Order",       Kind::Order,  0, (float) (kNumChainOrders - 1), 0, false, Unit::None, noChoices },
        { ID::pan,         "Pan",               Kind::Float,  -100.0f, 100.0f, 0.0f, false, Unit::Pan, noChoices },
        { ID::mute,        "Mute",              Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::detMode,     "Dyn Detector",      Kind::Choice, 0, 0, 1, false, Unit::None, detModeNames },
        { ID::detFreq,     "Dyn Detector Freq", Kind::Float,  20.0f, 20000.0f, 1000.0f, true, Unit::Hz, noChoices },
        { ID::detQ,        "Dyn Detector Q",    Kind::Float,  0.1f, 20.0f, 1.0f, true, Unit::Q, noChoices },
    };
    static_assert (sizeof (kBandSpecs) / sizeof (kBandSpecs[0]) == kNumBandParams, "band spec table out of sync");

    // Order must match GlobalParamIndex.
    const Spec kGlobalSpecs[] = {
        { ID::oversampling, "Oversampling",        Kind::Choice, 0, 0, 2, false, Unit::None, oversamplingNames },
        { ID::hpOn,         "High Pass",           Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::hpFreq,       "High Pass Frequency", Kind::Float,  10.0f, 2000.0f, 30.0f, true, Unit::Hz, noChoices },
        { ID::lpOn,         "Low Pass",            Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::lpFreq,       "Low Pass Frequency",  Kind::Float,  1000.0f, 30000.0f, 18000.0f, true, Unit::Hz, noChoices },
        { ID::filtSlope,    "Filter Slope",        Kind::Choice, 0, 0, 0, false, Unit::None, globalSlopeNames },
        { ID::gCompOn,      "Compressor",          Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::gCompThresh,  "Comp Threshold",      Kind::Float,  -60.0f, 0.0f, -12.0f, false, Unit::Db, noChoices },
        { ID::gCompRatio,   "Comp Ratio",          Kind::Float,  1.0f, 20.0f, 2.0f, true, Unit::Ratio, noChoices },
        { ID::gCompAttack,  "Comp Attack",         Kind::Float,  0.1f, 200.0f, 10.0f, true, Unit::Ms, noChoices },
        { ID::gCompRelease, "Comp Release",        Kind::Float,  5.0f, 2000.0f, 150.0f, true, Unit::Ms, noChoices },
        { ID::gCompMakeup,  "Comp Makeup",         Kind::Float,  0.0f, 24.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::gNoiseLevel,  "Noise Level",         Kind::Float,  kNoiseOffDb, 0.0f, kNoiseOffDb, false, Unit::NoiseDb, noChoices },
        { ID::gNoiseType,   "Noise Type",          Kind::Choice, 0, 0, 0, false, Unit::None, noiseTypeNames },
        { ID::gClipOn,      "Clipper",             Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::gClipCeiling, "Clipper Ceiling",     Kind::Float,  -24.0f, 0.0f, -0.3f, false, Unit::Db, noChoices },
        { ID::gClipSoft,    "Clipper Soft",        Kind::Bool,   0, 1, 1, false, Unit::None, noChoices },
        { ID::limitOn,      "Limiter",             Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::limitCeiling, "Limiter Ceiling",     Kind::Float,  -24.0f, 0.0f, -0.3f, false, Unit::Db, noChoices },
        { ID::limitRelease, "Limiter Release",     Kind::Float,  1.0f, 1000.0f, 80.0f, true, Unit::Ms, noChoices },
        { ID::mix,          "Mix",                 Kind::Float,  0.0f, 100.0f, 100.0f, false, Unit::Percent, noChoices },
        { ID::outGain,      "Output",              Kind::Float,  -24.0f, 24.0f, 0.0f, false, Unit::Db, noChoices },
        { ID::bypass,       "Bypass",              Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::lfo1Rate,     "LFO 1 Rate",          Kind::Float,  0.01f, 20.0f, 1.0f, true, Unit::Rate, noChoices },
        { ID::lfo1Shape,    "LFO 1 Shape",         Kind::Choice, 0, 0, 0, false, Unit::None, lfoShapeNames },
        { ID::lfo1Sync,     "LFO 1 Sync",          Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::lfo1Div,      "LFO 1 Division",      Kind::Choice, 0, 0, 6, false, Unit::None, lfoDivNames },
        { ID::lfo2Rate,     "LFO 2 Rate",          Kind::Float,  0.01f, 20.0f, 0.25f, true, Unit::Rate, noChoices },
        { ID::lfo2Shape,    "LFO 2 Shape",         Kind::Choice, 0, 0, 1, false, Unit::None, lfoShapeNames },
        { ID::lfo2Sync,     "LFO 2 Sync",          Kind::Bool,   0, 1, 0, false, Unit::None, noChoices },
        { ID::lfo2Div,      "LFO 2 Division",      Kind::Choice, 0, 0, 8, false, Unit::None, lfoDivNames },
        { ID::envAttack,    "Envelope Attack",     Kind::Float,  0.1f, 500.0f, 10.0f, true, Unit::Ms, noChoices },
        { ID::envRelease,   "Envelope Release",    Kind::Float,  5.0f, 3000.0f, 150.0f, true, Unit::Ms, noChoices },
    };
    static_assert (sizeof (kGlobalSpecs) / sizeof (kGlobalSpecs[0]) == kNumGlobalParams, "global spec table out of sync");

    // Order must match ModSlotParamIndex.
    const Spec kModSpecs[] = {
        { ID::modSource, "Source", Kind::Choice, 0, 0, 0, false, Unit::None, modSourceNames },
        { ID::modBand,   "Band",   Kind::Choice, 0, 0, 0, false, Unit::None, modBandNames },
        { ID::modTarget, "Target", Kind::Choice, 0, 0, 0, false, Unit::None, modTargetNames },
        { ID::modAmount, "Amount", Kind::Float,  -100.0f, 100.0f, 0.0f, false, Unit::Percent, noChoices },
    };
    static_assert (sizeof (kModSpecs) / sizeof (kModSpecs[0]) == kNumModSlotParams, "mod spec table out of sync");

    juce::String orderText (int code)
    {
        int order[kNumModules];
        decodeChainOrder (code, order);
        static const char* shortNames[] = { "Flt", "Drv", "Pht", "Noi", "FX", "Cmp", "Clp" };
        juce::String t;
        for (int i = 0; i < kNumModules; ++i) t << (i > 0 ? " > " : "") << shortNames[order[i]];
        return t;
    }

    juce::NormalisableRange<float> makeRange (const Spec& s)
    {
        if (s.logScale)
            return juce::NormalisableRange<float> (
                s.lo, s.hi,
                [] (float a, float b, float x) { return a * std::pow (b / a, x); },
                [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                [] (float a, float b, float v) { return juce::jlimit (a, b, v); });
        return juce::NormalisableRange<float> (s.lo, s.hi, 0.0f);
    }

    juce::String formatValue (Unit u, float v)
    {
        switch (u)
        {
            case Unit::Hz:      return formatFrequency (v);
            case Unit::Db:      return formatDb (v);
            case Unit::NoiseDb: return v <= kNoiseOffDb + 0.01f ? juce::String ("Off") : formatDb (v);
            case Unit::Percent: return formatNumber (v, 0) + " %";
            case Unit::Ms:      return formatNumber (v, v < 10.0f ? 1 : 0) + " ms";
            case Unit::Q:       return formatNumber (v, v < 1.0f ? 3 : 2);
            case Unit::Ratio:   return formatNumber (v, v < 10.0f ? 1 : 0) + ":1";
            case Unit::Bias:    return formatNumber (v, 2);
            case Unit::Rate:    return formatNumber (v, v < 1.0f ? 2 : (v < 10.0f ? 1 : 0)) + " Hz";
            case Unit::Pan:
            {
                const int pc = (int) std::lround (v);
                return pc == 0 ? juce::String ("C") : (pc < 0 ? "L" + juce::String (-pc) : "R" + juce::String (pc));
            }
            case Unit::None:
            default:            return juce::String (v, 2);
        }
    }

    std::unique_ptr<juce::RangedAudioParameter> makeParam (const Spec& s, const juce::String& id, const juce::String& name)
    {
        using namespace juce;
        const ParameterID pid { id, 1 };
        if (s.kind == Kind::Bool)
            return std::make_unique<AudioParameterBool> (pid, name, s.def > 0.5f);
        if (s.kind == Kind::Choice)
            return std::make_unique<AudioParameterChoice> (pid, name, s.choices(), (int) s.def);
        if (s.kind == Kind::Order)
            return std::make_unique<AudioParameterInt> (pid, name, (int) s.lo, (int) s.hi, (int) s.def,
                                                        AudioParameterIntAttributes().withAutomatable (false)
                                                            .withStringFromValueFunction ([] (int v, int) { return orderText (v); }));

        const Unit unit = s.unit;
        auto attr = AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([unit] (float v, int) { return formatValue (unit, v); })
                        .withValueFromStringFunction ([unit] (const String& t)
                        {
                            if (unit == Unit::NoiseDb && t.trim().equalsIgnoreCase ("off")) return kNoiseOffDb;
                            if (unit == Unit::Pan)
                            {
                                const auto u = t.trim().toUpperCase();
                                if (u == "C") return 0.0f;
                                const float v = (float) std::abs (parseNumber (u));
                                return u.startsWith ("L") ? -v : v;
                            }
                            return (float) parseNumber (t);
                        });
        return std::make_unique<AudioParameterFloat> (pid, name, makeRange (s), s.def, attr);
    }
}

juce::String bandParamId (int band, int index)
{
    return ID::band (band, kBandSpecs[juce::jlimit (0, kNumBandParams - 1, index)].id);
}

juce::String globalParamId (int index)
{
    return kGlobalSpecs[juce::jlimit (0, kNumGlobalParams - 1, index)].id;
}

juce::String modParamId (int slot, int index)
{
    return ID::modSlot (juce::jlimit (0, kNumModSlots - 1, slot), kModSpecs[juce::jlimit (0, kNumModSlotParams - 1, index)].id);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto prefix = "Band " + juce::String (b + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1), prefix.trim(), "|");
        for (int i = 0; i < kNumBandParams; ++i)
        {
            Spec s = kBandSpecs[i];
            if (i == bpFreq || i == bpDetFreq)   // spread the default frequencies of the slots
                s.def = (float) juce::jlimit (20.0, 20000.0, 60.0 * std::pow (250.0, b / (double) (kNumBands - 1)));
            group->addChild (makeParam (s, ID::band (b, s.id), prefix + s.name));
        }
        layout.add (std::move (group));
    }

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");
    for (int i = 0; i < kNumGlobalParams; ++i)
        global->addChild (makeParam (kGlobalSpecs[i], kGlobalSpecs[i].id, i == gpBypass ? juce::String ("Bypass") : "Global " + juce::String (kGlobalSpecs[i].name)));
    layout.add (std::move (global));

    for (int m = 0; m < kNumModSlots; ++m)
    {
        const auto prefix = "Mod " + juce::String (m + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("mod" + juce::String (m + 1), prefix.trim(), "|");
        for (int i = 0; i < kNumModSlotParams; ++i)
            group->addChild (makeParam (kModSpecs[i], modParamId (m, i), prefix + kModSpecs[i].name));
        layout.add (std::move (group));
    }
    return layout;
}

// ---------------------------------------------------------------------------
void ParamRefs::init (juce::AudioProcessorValueTreeState& s)
{
    for (int b = 0; b < kNumBands; ++b)
        for (int i = 0; i < kNumBandParams; ++i)
        {
            bands[(size_t) b].p[i] = s.getRawParameterValue (bandParamId (b, i));
            jassert (bands[(size_t) b].p[i] != nullptr);
        }
    for (int i = 0; i < kNumGlobalParams; ++i)
    {
        g[i] = s.getRawParameterValue (globalParamId (i));
        jassert (g[i] != nullptr);
    }
    for (int slot = 0; slot < kNumModSlots; ++slot)
        for (int i = 0; i < kNumModSlotParams; ++i)
        {
            m[slot][i] = s.getRawParameterValue (modParamId (slot, i));
            jassert (m[slot][i] != nullptr);
        }
}

namespace
{
    inline float rd (std::atomic<float>* a) noexcept { return a->load (std::memory_order_relaxed); }
    inline bool  on (std::atomic<float>* a) noexcept { return rd (a) > 0.5f; }
    inline int   idx (std::atomic<float>* a, int count) noexcept { return juce::jlimit (0, count - 1, (int) std::lround (rd (a))); }
    inline double clampD (std::atomic<float>* a, double lo, double hi) noexcept
    {
        const double v = (double) rd (a);
        return std::isfinite (v) ? juce::jlimit (lo, hi, v) : lo;
    }
}

BandParams ParamRefs::readBand (int b) const noexcept
{
    const auto& r = bands[(size_t) juce::jlimit (0, kNumBands - 1, b)].p;
    BandParams p;
    p.used        = on (r[bpUsed]);
    p.enabled     = on (r[bpEnabled]);
    p.solo        = on (r[bpSolo]);
    p.shape       = (Shape) idx (r[bpShape], (int) Shape::Count);
    p.freq        = clampD (r[bpFreq], 10.0, 30000.0);
    p.q           = clampD (r[bpQ], 0.1, 20.0);
    p.slope       = idx (r[bpSlope], kNumRegionSlopes);
    p.type        = (DistType) idx (r[bpType], (int) DistType::Count);
    p.driveDb     = clampD (r[bpDrive], 0.0, 48.0);
    p.bias        = clampD (r[bpBias], -1.0, 1.0);
    p.autoGain    = on (r[bpAutoGain]);
    p.filterType  = (BandFilter) idx (r[bpFilterType], (int) BandFilter::Count);
    p.filterSlope = idx (r[bpFilterSlope], kNumFilterSlopes);
    p.filterFreq  = clampD (r[bpFilterFreq], 20.0, 20000.0);
    p.filterRes   = clampD (r[bpFilterRes], 0.1, 10.0);
    p.filterGainDb = clampD (r[bpFilterGain], -24.0, 24.0);
    p.phat        = clampD (r[bpPhat], 0.0, 100.0) / 100.0;
    p.noiseDb     = clampD (r[bpNoiseLevel], kNoiseOffDb, 0.0);
    p.noiseType   = (NoiseType) idx (r[bpNoiseType], (int) NoiseType::Count);
    p.noiseFollow = clampD (r[bpNoiseFollow], 0.0, 100.0) / 100.0;
    p.compOn      = on (r[bpCompOn]);
    p.compThresh  = clampD (r[bpCompThresh], -60.0, 0.0);
    p.compRatio   = clampD (r[bpCompRatio], 1.0, 20.0);
    p.compAttack  = clampD (r[bpCompAttack], 0.1, 200.0);
    p.compRelease = clampD (r[bpCompRelease], 5.0, 2000.0);
    p.compMakeup  = clampD (r[bpCompMakeup], 0.0, 24.0);
    p.clipMode    = (ClipMode) idx (r[bpClipMode], (int) ClipMode::Count);
    p.clipCeiling = clampD (r[bpClipCeiling], -24.0, 0.0);
    p.wet         = clampD (r[bpWet], 0.0, 100.0) / 100.0;
    p.gainDb      = clampD (r[bpGain], -24.0, 24.0);
    p.dynOn       = on (r[bpDynOn]);
    p.dynSC       = on (r[bpDynSC]);
    p.dynThresh   = clampD (r[bpDynThresh], -80.0, 6.0);
    p.dynAttack   = clampD (r[bpDynAttack], 0.1, 200.0);
    p.dynRelease  = clampD (r[bpDynRelease], 5.0, 3000.0);
    p.dynGain     = clampD (r[bpDynGain], -24.0, 24.0);
    p.dynDrive    = clampD (r[bpDynDrive], -48.0, 48.0);
    p.dynWet      = clampD (r[bpDynWet], -100.0, 100.0) / 100.0;
    p.fxType      = (FxType) idx (r[bpFxType], (int) FxType::Count);
    p.fxRate      = clampD (r[bpFxRate], 0.02, 20.0);
    p.fxDepth     = clampD (r[bpFxDepth], 0.0, 100.0) / 100.0;
    p.fxFeedback  = clampD (r[bpFxFeedback], -95.0, 95.0) / 100.0;
    p.fxMix       = clampD (r[bpFxMix], 0.0, 100.0) / 100.0;
    p.chainCode   = idx (r[bpChainOrder], kNumChainOrders);
    decodeChainOrder (p.chainCode, p.order);
    p.pan         = clampD (r[bpPan], -100.0, 100.0) / 100.0;
    p.mute        = on (r[bpMute]);
    p.detMode     = (DetMode) idx (r[bpDetMode], (int) DetMode::Count);
    p.detFreq     = clampD (r[bpDetFreq], 20.0, 20000.0);
    p.detQ        = clampD (r[bpDetQ], 0.1, 20.0);
    return p;
}

GlobalParams ParamRefs::readGlobal() const noexcept
{
    GlobalParams p;
    p.osLog2       = idx (g[gpOversampling], 4);
    p.hpOn         = on (g[gpHpOn]);
    p.hpFreq       = clampD (g[gpHpFreq], 10.0, 2000.0);
    p.lpOn         = on (g[gpLpOn]);
    p.lpFreq       = clampD (g[gpLpFreq], 1000.0, 30000.0);
    p.filtSlope    = idx (g[gpFiltSlope], 2);
    p.compOn       = on (g[gpCompOn]);
    p.compThresh   = clampD (g[gpCompThresh], -60.0, 0.0);
    p.compRatio    = clampD (g[gpCompRatio], 1.0, 20.0);
    p.compAttack   = clampD (g[gpCompAttack], 0.1, 200.0);
    p.compRelease  = clampD (g[gpCompRelease], 5.0, 2000.0);
    p.compMakeup   = clampD (g[gpCompMakeup], 0.0, 24.0);
    p.noiseDb      = clampD (g[gpNoiseLevel], kNoiseOffDb, 0.0);
    p.noiseType    = (NoiseType) idx (g[gpNoiseType], (int) NoiseType::Count);
    p.clipOn       = on (g[gpClipOn]);
    p.clipCeiling  = clampD (g[gpClipCeiling], -24.0, 0.0);
    p.clipSoft     = on (g[gpClipSoft]);
    p.limitOn      = on (g[gpLimitOn]);
    p.limitCeiling = clampD (g[gpLimitCeiling], -24.0, 0.0);
    p.limitRelease = clampD (g[gpLimitRelease], 1.0, 1000.0);
    p.mix          = clampD (g[gpMix], 0.0, 100.0) / 100.0;
    p.outGainDb    = clampD (g[gpOutGain], -24.0, 24.0);
    p.bypass       = on (g[gpBypass]);
    p.lfoRate[0]   = clampD (g[gpLfo1Rate], 0.01, 20.0);
    p.lfoShape[0]  = (LfoShape) idx (g[gpLfo1Shape], (int) LfoShape::Count);
    p.lfoSync[0]   = on (g[gpLfo1Sync]);
    p.lfoDiv[0]    = idx (g[gpLfo1Div], kNumLfoDivs);
    p.lfoRate[1]   = clampD (g[gpLfo2Rate], 0.01, 20.0);
    p.lfoShape[1]  = (LfoShape) idx (g[gpLfo2Shape], (int) LfoShape::Count);
    p.lfoSync[1]   = on (g[gpLfo2Sync]);
    p.lfoDiv[1]    = idx (g[gpLfo2Div], kNumLfoDivs);
    p.envAttack    = clampD (g[gpEnvAttack], 0.1, 500.0);
    p.envRelease   = clampD (g[gpEnvRelease], 5.0, 3000.0);
    return p;
}

ModSlot ParamRefs::readModSlot (int slot) const noexcept
{
    const auto& r = m[juce::jlimit (0, kNumModSlots - 1, slot)];
    ModSlot s;
    s.source = (ModSource) idx (r[mpSource], (int) ModSource::Count);
    s.band   = idx (r[mpBand], kNumBands + 1) - 1;
    s.target = (ModTarget) idx (r[mpTarget], (int) ModTarget::Count);
    s.amount = clampD (r[mpAmount], -100.0, 100.0) / 100.0;
    return s;
}

} // namespace gkd
