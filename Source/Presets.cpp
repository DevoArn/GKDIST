// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Presets.h"

namespace gkd::presets
{

StateBuilder& StateBuilder::set (const juce::String& id, float value)
{
    for (auto child : state)
        if (child.getProperty ("id").toString() == id)
        {
            child.setProperty ("value", value, nullptr);
            return *this;
        }
    juce::ValueTree p ("PARAM");
    p.setProperty ("id", id, nullptr);
    p.setProperty ("value", value, nullptr);
    state.addChild (p, -1, nullptr);
    return *this;
}

StateBuilder& StateBuilder::add (int b, Shape shape, double freq, double q, DistType type, double driveDb, double wetPercent)
{
    band (b, bpUsed, 1.0f);
    band (b, bpEnabled, 1.0f);
    band (b, bpShape, (float) (int) shape);
    band (b, bpFreq, (float) freq);
    band (b, bpQ, (float) q);
    band (b, bpType, (float) (int) type);
    band (b, bpDrive, (float) driveDb);
    band (b, bpWet, (float) wetPercent);
    return *this;
}

juce::StringArray factoryNames()
{
    return { "Default",
             "Warm Glue (Tape, full band)",
             "Bass Phat",
             "Vocal Grit",
             "Drum Smash",
             "Lo-Fi Crusher",
             "Dynamic Saturation (only when loud)",
             "Sizzle Highs",
             "Fuzz Lead",
             "Master Loudness (clip + limit)" };
}

juce::ValueTree makeFactory (int index, const juce::ValueTree& defaults)
{
    StateBuilder s (defaults.createCopy());
    switch (index)
    {
        case 1:
            s.add (0, Shape::Full, 1000, 1, DistType::Tape, 9, 60);
            break;
        case 2:
            s.add (0, Shape::Low, 160, 1, DistType::Tube, 10, 80);
            s.band (0, bpPhat, 60.0f).band (0, bpSlope, 1.0f);
            s.add (1, Shape::Bell, 900, 1.2, DistType::Transistor, 6, 30);
            break;
        case 3:
            s.add (0, Shape::Bell, 2500, 0.9, DistType::Tape, 15, 45);
            s.add (1, Shape::High, 7000, 1, DistType::SoftClip, 8, 25);
            s.band (1, bpNoiseLevel, -62.0f).band (1, bpNoiseType, (float) (int) NoiseType::Pink).band (1, bpNoiseFollow, 100.0f);
            break;
        case 4:
            s.add (0, Shape::Full, 1000, 1, DistType::HardClip, 14, 70);
            s.band (0, bpCompOn, 1.0f).band (0, bpCompThresh, -20.0f).band (0, bpCompRatio, 6.0f).band (0, bpCompAttack, 3.0f);
            s.add (1, Shape::Low, 90, 1, DistType::Tube, 8, 60);
            s.global (gpLimitOn, 1.0f).global (gpLimitCeiling, -0.5f);
            break;
        case 5:
            s.add (0, Shape::Full, 1000, 1, DistType::Bitcrush, 28, 100);
            s.band (0, bpBias, 0.25f).band (0, bpNoiseLevel, -42.0f).band (0, bpNoiseType, (float) (int) NoiseType::Crackle);
            s.global (gpLpOn, 1.0f).global (gpLpFreq, 7000.0f).global (gpHpOn, 1.0f).global (gpHpFreq, 120.0f);
            break;
        case 6:
            s.add (0, Shape::Bell, 1200, 0.8, DistType::Tube, 6, 0);
            s.band (0, bpDynOn, 1.0f).band (0, bpDynThresh, -26.0f).band (0, bpDynWet, 100.0f).band (0, bpDynDrive, 12.0f);
            break;
        case 7:
            s.add (0, Shape::High, 6000, 1, DistType::Foldback, 12, 35);
            s.band (0, bpSlope, 1.0f).band (0, bpFilterType, (float) (int) BandFilter::HighPass).band (0, bpFilterFreq, 5000.0f);
            break;
        case 8:
            s.add (0, Shape::Bell, 1500, 0.7, DistType::Fuzz, 22, 85);
            s.band (0, bpFilterType, (float) (int) BandFilter::LowPass).band (0, bpFilterPos, 1.0f).band (0, bpFilterFreq, 6000.0f);
            s.band (0, bpClipMode, (float) (int) ClipMode::Soft).band (0, bpClipCeiling, -3.0f);
            break;
        case 9:
            s.add (0, Shape::Full, 1000, 1, DistType::SoftClip, 4, 100);
            s.global (gpClipOn, 1.0f).global (gpClipCeiling, -1.0f).global (gpClipSoft, 1.0f);
            s.global (gpLimitOn, 1.0f).global (gpLimitCeiling, -0.3f);
            break;
        default:
            break;
    }
    return s.get();
}

juce::File userFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GKDIST").getChildFile ("Presets");
    if (! f.exists()) f.createDirectory();
    return f;
}

juce::Array<juce::File> listUserPresets()
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.gkdpreset");
    files.sort();
    return files;
}

bool saveUserPreset (const juce::ValueTree& params, const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto xml = params.createXml();
    return xml != nullptr && xml->writeTo (userFolder().getChildFile (safe + ".gkdpreset"));
}

juce::ValueTree loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        return juce::ValueTree::fromXml (*xml);
    return {};
}

} // namespace gkd::presets
