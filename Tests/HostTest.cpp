// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

// Loads the built VST3 like a DAW would, exercises it and renders the editor to PNG.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <complex>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("  ok: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 512;

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100) == name) return prm;
    std::printf ("  (parameter '%s' not found)\n", name.toRawUTF8());
    ++failures;
    return nullptr;
}

/** Sets a normalised value (bools: 0/1, choices: index / (count - 1)). */
static void setNorm (juce::AudioPluginInstance& p, const juce::String& name, float v01)
{
    if (auto* prm = findParam (p, name)) prm->setValueNotifyingHost (v01);
}

/** Sets a value in display units by searching the normalised value whose text matches
    (hosted VST3 parameters do not expose their range). */
static void setValue (juce::AudioPluginInstance& p, const juce::String& name, double value)
{
    auto* prm = findParam (p, name);
    if (prm == nullptr) return;
    float best = 0.0f; double bestErr = 1.0e30;
    for (int i = 0; i <= 4000; ++i)
    {
        const float v = i / 4000.0f;
        const auto txt = prm->getText (v, 64);
        const double t = txt.retainCharacters ("-0123456789.").getDoubleValue() * (txt.containsIgnoreCase ("khz") ? 1000.0 : 1.0);
        const double e = std::abs (t - value);
        if (e < bestErr) { bestErr = e; best = v; }
    }
    prm->setValueNotifyingHost (best);
}

/** Runs `blocks` blocks through the plug-in. gen (n) gives the input sample at absolute index n. */
template <typename Gen>
static void run (juce::AudioPluginInstance& p, int blocks, Gen gen, std::vector<float>* in, std::vector<float>* out)
{
    static long long n0 = 0;
    juce::AudioBuffer<float> buf (p.getTotalNumInputChannels(), kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        for (int i = 0; i < kBlock; ++i)
        {
            const float x = gen (n0 + i);
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
            if (in != nullptr) in->push_back (x);
        }
        n0 += kBlock;
        p.processBlock (buf, midi);
        if (out != nullptr)
            for (int i = 0; i < kBlock; ++i) out->push_back (buf.getSample (0, i));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

/** Hann-windowed amplitude (dBFS of a sine) at frequency f over x[start, start + len). */
static double toneDb (const std::vector<float>& x, size_t start, size_t len, double f)
{
    std::complex<double> s = 0.0;
    double wsum = 0.0;
    for (size_t n = 0; n < len; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) n / (double) (len - 1));
        s += w * (double) x[start + n] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / kRate * (double) n);
        wsum += w;
    }
    return 20.0 * std::log10 (2.0 * std::abs (s) / wsum + 1.0e-12);
}

static float sine (long long n, double f, double amp)
{
    return (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) n / kRate));
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String path = argc > 1 ? juce::String (argv[1]) : juce::String ("build/GKDIST_artefacts/Release/VST3/GKDIST.vst3");
    const juce::String pngPath = argc > 2 ? juce::String (argv[2]) : juce::String ("editor.png");

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->getName() == "VST3")
            f->findAllTypesForFile (types, juce::File::getCurrentWorkingDirectory().getChildFile (path).getFullPathName());

    std::printf ("[1] Load\n");
    CHECK (types.size() == 1, "found %d plugin type(s) in the bundle", types.size());
    if (types.isEmpty()) return 1;
    std::printf ("  %s by %s, category '%s'\n", types[0]->name.toRawUTF8(), types[0]->manufacturerName.toRawUTF8(), types[0]->category.toRawUTF8());
    CHECK (types[0]->name == "GKDIST" && types[0]->manufacturerName == "GK", "name and manufacturer");

    juce::String err;
    auto plugin = fm.createPluginInstance (*types[0], kRate, kBlock, err);
    CHECK (plugin != nullptr, "instantiated %s", err.toRawUTF8());
    if (plugin == nullptr) return 1;

    std::printf ("[2] Buses & parameters\n");
    std::printf ("  inputs: %d buses, outputs: %d buses, params: %d\n", plugin->getBusCount (true), plugin->getBusCount (false), plugin->getParameters().size());
    CHECK (plugin->getBusCount (true) == 2, "side-chain input bus present");
    CHECK (plugin->getParameters().size() >= 24 * 37 + 23, "parameter count %d", plugin->getParameters().size());

    plugin->setPlayConfigDetails (4, 2, kRate, kBlock);
    plugin->prepareToPlay (kRate, kBlock);
    run (*plugin, 20, [] (long long) { return 0.0f; }, nullptr, nullptr);

    auto latencyAfterOs = [&] (float norm, const char* label, int expected)
    {
        setNorm (*plugin, "Global Oversampling", norm);
        run (*plugin, 40, [] (long long) { return 0.0f; }, nullptr, nullptr);
        CHECK (plugin->getLatencySamples() == expected, "latency with oversampling %s = %d (expected %d)", label, plugin->getLatencySamples(), expected);
    };

    std::printf ("[3] Latency per oversampling factor\n");
    CHECK (plugin->getLatencySamples() == 126, "default 4x latency = %d", plugin->getLatencySamples());
    latencyAfterOs (0.0f, "off", 48);
    latencyAfterOs (1.0f / 3.0f, "2x", 119);
    latencyAfterOs (1.0f, "8x", 129);
    latencyAfterOs (2.0f / 3.0f, "4x", 126);

    auto transparency = [&] (const char* label, double limit)
    {
        std::vector<float> in, out;
        // Band-limited test signal: the oversampler's half-band filters roll off
        // above ~20 kHz by design, so white noise up to 24 kHz would not null.
        run (*plugin, 60, [] (long long n)
        {
            return sine (n, 97.0, 0.2) + sine (n, 1234.5, 0.15) + sine (n, 5021.0, 0.1) + sine (n, 15013.0, 0.05);
        }, &in, &out);
        const int lat = plugin->getLatencySamples();
        double maxErr = 0.0;
        for (size_t n = 8192; n < out.size(); ++n)
            maxErr = std::max (maxErr, (double) std::abs (out[n] - in[n - (size_t) lat]));
        CHECK (maxErr < limit, "%s: output = input delayed by %d samples (max dev %.2e)", label, lat, maxErr);
    };

    std::printf ("[4] Transparent with no bands\n");
    transparency ("4x oversampling", 1.0e-4);
    latencyAfterOs (0.0f, "off", 48);
    transparency ("no oversampling", 1.0e-6);
    latencyAfterOs (2.0f / 3.0f, "4x", 126);

    std::printf ("[5] Distortion via host parameters\n");
    {
        setNorm (*plugin, "Band 1 Shape", 1.0f);                  // Full
        setNorm (*plugin, "Band 1 Distortion Type", 1.0f / 9.0f);  // Hard Clip
        setValue (*plugin, "Band 1 Drive", 24.0);
        setNorm (*plugin, "Band 1 Used", 1.0f);
        std::vector<float> out;
        run (*plugin, 40, [] (long long n) { return sine (n, 1000.0, 0.25); }, nullptr, nullptr);
        run (*plugin, 40, [] (long long n) { return sine (n, 1000.0, 0.25); }, nullptr, &out);
        const size_t s = 2048, len = 16384;
        const double h1 = toneDb (out, s, len, 1000.0), h3 = toneDb (out, s, len, 3000.0), h5 = toneDb (out, s, len, 5000.0);
        bool finite = true; float peak = 0.0f;
        for (float v : out) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        std::printf ("  1 kHz %.1f dBFS, 3rd harmonic %.1f dBc, 5th %.1f dBc, peak %.3f\n", h1, h3 - h1, h5 - h1, peak);
        CHECK (finite, "output is finite");
        CHECK (h3 - h1 > -20.0 && h5 - h1 > -30.0, "hard clipping produces strong odd harmonics");
        CHECK (std::abs (h1 - 20.0 * std::log10 (0.25)) < 6.0, "auto gain keeps the level near the input (%.1f dBFS)", h1);
    }

    std::printf ("[6] Limiter ceiling\n");
    {
        setNorm (*plugin, "Global Limiter", 1.0f);
        setValue (*plugin, "Global Limiter Ceiling", -3.0);
        setValue (*plugin, "Global Output", 18.0);
        std::vector<float> out;
        juce::Random rng (8);
        run (*plugin, 30, [&rng] (long long n) { return sine (n, 110.0, 0.7) + (rng.nextFloat() - 0.5f) * 0.4f; }, nullptr, nullptr);
        run (*plugin, 60, [&rng] (long long n) { return sine (n, 110.0, 0.7) + (rng.nextFloat() - 0.5f) * 0.4f; }, nullptr, &out);
        float peak = 0.0f;
        for (float v : out) peak = std::max (peak, std::abs (v));
        const double peakDb = 20.0 * std::log10 (peak + 1.0e-12);
        CHECK (peakDb <= -3.0 + 0.01 && peakDb > -4.0, "output peak %.3f dBFS with +18 dB output gain into a -3 dB ceiling", peakDb);
        setValue (*plugin, "Global Output", 0.0);
        setNorm (*plugin, "Global Limiter", 0.0f);
    }

    std::printf ("[7] State round trip\n");
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto* d = findParam (*plugin, "Band 1 Drive");
        if (d != nullptr) d->setValueNotifyingHost (0.0f);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        const auto txt = d != nullptr ? d->getCurrentValueAsText() : juce::String();
        CHECK (txt.contains ("24.0"), "drive restored from state ('%s'), %d bytes", txt.toRawUTF8(), (int) state.getSize());
    }

    std::printf ("[8] Editor\n");
    {
        auto band = [&] (int b, float shape, double f, double drive, float type)
        {
            const auto pre = "Band " + juce::String (b) + " ";
            setNorm (*plugin, pre + "Shape", shape / 3.0f);
            setValue (*plugin, pre + "Frequency", f);
            setValue (*plugin, pre + "Drive", drive);
            setNorm (*plugin, pre + "Distortion Type", type / 9.0f);
            setNorm (*plugin, pre + "Used", 1.0f);
        };
        setNorm (*plugin, "Band 1 Used", 0.0f);
        band (2, 1.0f, 120.0, 12.0, 2.0f);    // low region, tube
        band (3, 0.0f, 900.0, 20.0, 3.0f);    // bell, tape
        band (4, 2.0f, 6000.0, 8.0, 7.0f);    // high region, foldback

        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
        CHECK (ed != nullptr, "editor created");
        if (ed != nullptr)
        {
            std::printf ("  editor size %d x %d\n", ed->getWidth(), ed->getHeight());
            juce::Random rng (11);
            float b0 = 0, b1 = 0, b2 = 0;
            run (*plugin, 200, [&] (long long)
            {
                const float w = rng.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                return 0.08f * (b0 + b1 + b2 + w * 0.1848f);
            }, nullptr, nullptr);
            for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (pngPath);
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat png;
            CHECK (png.writeImageToStream (img, os), "editor rendered to %s", out.getFullPathName().toRawUTF8());
            plugin->editorBeingDeleted (ed.get());
        }
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "HOST TEST PASSED" : "HOST TEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
