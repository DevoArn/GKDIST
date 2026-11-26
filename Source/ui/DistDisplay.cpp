// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DistDisplay.h"
#include "../dsp/RegionDesign.h"
#include <cstring>

namespace gkd::ui
{

namespace
{
    constexpr float kNodeR = 7.5f, kHandleR = 7.0f;
    const double kLogSpan = std::log (kDispMaxF / kDispMinF);

    juce::String shortFreq (double f)
    {
        if (f >= 1000.0)
        {
            const double k = f / 1000.0;
            const bool whole = std::abs (k - std::round (k)) < 1.0e-6;
            return (whole ? juce::String ((int) std::lround (k)) : juce::String (k, 1)) + "k";
        }
        return juce::String ((int) std::lround (f));
    }

    /** Detector filter shape, exactly as the band processor builds it. */
    dsp::DesignParams detectorDesign (const BandParams& p)
    {
        dsp::DesignParams dp;
        const bool own = p.detMode == DetMode::Bell;
        dp.freq = own ? p.detFreq : p.freq;
        dp.q = own ? p.detQ : p.q;
        if (own) dp.type = FilterType::Bell;
        else
            switch (p.shape)
            {
                case Shape::Bell: dp.type = FilterType::Bell; break;
                case Shape::Low:  dp.type = FilterType::LowShelf; break;
                case Shape::High: dp.type = FilterType::HighShelf; break;
                case Shape::Full:
                case Shape::Count:
                default:          dp.type = FilterType::FlatTilt; break;
            }
        return dp;
    }
}

DistDisplay::DistDisplay (GKDistProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    mixColour.fill (juce::Colours::transparentBlack);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

DistDisplay::~DistDisplay() { stopTimer(); }

void DistDisplay::resized() { plot = getLocalBounds().toFloat(); }

// ---------------------------------------------------------------------------
float DistDisplay::freqToX (double f) const noexcept
{
    return plot.getX() + plot.getWidth() * (float) (std::log (std::max (1.0, f) / kDispMinF) / kLogSpan);
}

double DistDisplay::xToFreq (float x) const noexcept
{
    const double t = (x - plot.getX()) / std::max (1.0f, plot.getWidth());
    return juce::jlimit (10.0, 30000.0, kDispMinF * std::exp (t * kLogSpan));
}

float DistDisplay::driveToY (double db) const noexcept
{
    const float top = plot.getY() + 22.0f, bottom = plot.getBottom() - 24.0f;
    return bottom - (float) (juce::jlimit (0.0, kMaxDriveDb, db) / kMaxDriveDb) * (bottom - top);
}

double DistDisplay::yToDrive (float y) const noexcept
{
    const float top = plot.getY() + 22.0f, bottom = plot.getBottom() - 24.0f;
    return juce::jlimit (0.0, kMaxDriveDb, (double) ((bottom - y) / std::max (1.0f, bottom - top)) * kMaxDriveDb);
}

float DistDisplay::specToY (float db) const noexcept
{
    const float range = (float) (int) proc.uiState.getProperty ("anaRange", 90);
    return plot.getY() + juce::jlimit (0.0f, plot.getHeight(), (-db / range) * plot.getHeight());
}

double DistDisplay::yToSpec (float y) const noexcept
{
    const double range = (double) (int) proc.uiState.getProperty ("anaRange", 90);
    return -(double) (y - plot.getY()) / std::max (1.0f, plot.getHeight()) * range;
}

float DistDisplay::filterDbToY (double db) const noexcept
{
    const float mid = driveToY (0.5 * kMaxDriveDb), half = 0.5f * (driveToY (0.0) - driveToY (kMaxDriveDb));
    return mid - (float) (juce::jlimit (-kFilterRangeDb, kFilterRangeDb, db) / kFilterRangeDb) * half;
}

double DistDisplay::yToFilterDb (float y) const noexcept
{
    const float mid = driveToY (0.5 * kMaxDriveDb), half = 0.5f * (driveToY (0.0) - driveToY (kMaxDriveDb));
    return juce::jlimit (-kFilterRangeDb, kFilterRangeDb, (double) ((mid - y) / std::max (1.0f, half)) * kFilterRangeDb);
}

juce::Point<float> DistDisplay::nodePosition (int b) const
{
    return { freqToX (bands[b].freq), driveToY (bands[b].driveDb) };
}

juce::Point<float> DistDisplay::filterHandlePosition (int b) const
{
    const auto& p = bands[b];
    return { freqToX (p.filterFreq), filterDbToY (filterHasGain (p.filterType) ? p.filterGainDb : 0.0) };
}

juce::Point<float> DistDisplay::detectorHandlePosition (int b) const
{
    const auto& p = bands[b];
    return { freqToX (p.detMode == DetMode::Bell ? p.detFreq : p.freq), specToY ((float) p.dynThresh) };
}

// ---------------------------------------------------------------------------
void DistDisplay::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.001, 0.1, (now - lastTime) * 0.001);
    lastTime = now;
    animTime += dt;

    const auto& ui = proc.uiState;
    const double fs = proc.getCurrentSampleRate();
    const int res = (int) ui.getProperty ("anaRes", 2), speed = (int) ui.getProperty ("anaSpeed", 2);
    const double tilt = (double) ui.getProperty ("anaTilt", 4.5);
    pre.configure (res, fs);
    post.configure (res, fs);
    if (! (bool) ui.getProperty ("anaFreeze", false))
    {
        if ((bool) ui.getProperty ("anaPre", true))  pre.process (proc.preTap, dt, speed, tilt);
        if ((bool) ui.getProperty ("anaPost", true)) post.process (proc.postTap, dt, speed, tilt);
    }

    const bool running = proc.isAudioRunning();
    const float actAlpha = (float) (1.0 - std::exp (-dt / 0.06));
    for (int b = 0; b < kNumBands; ++b)
    {
        bands[b] = proc.refs.readBand (b);
        tel[b] = proc.getTelemetry (b);
        const auto& p = bands[b];
        const bool live = running && p.used;
        liveDrive[(size_t) b] = live ? juce::jlimit (0.0f, (float) kMaxDriveDb, tel[b].driveDb) : (float) p.driveDb;
        const float wet = live ? tel[b].wet : (float) p.wet;
        float inten = juce::jlimit (0.0f, 1.0f, liveDrive[(size_t) b] / 36.0f) * (0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, wet));
        if (! p.enabled) inten *= 0.3f;
        intensity[(size_t) b] = p.used ? inten : 0.0f;
        const float act = live && p.enabled ? juce::jlimit (0.0f, 1.0f, (tel[b].levelDb + 42.0f) / 42.0f) : 0.0f;
        activity[(size_t) b] += actAlpha * (act - activity[(size_t) b]);
    }
    if (selected >= 0 && ! bands[selected].used)
        selectBand (-1);
    updateRegions();
    updateSelectedCurves();
    updateMixAndClashes();

    if (wheelCommit > 0 && --wheelCommit == 0) notifyEdit();
    const int hz = (bool) ui.getProperty ("gpu", true) ? 60 : 40;
    if (getTimerInterval() != 1000 / hz) startTimerHz (hz);
    repaint();
}

void DistDisplay::updateRegions()
{
    const double fs = proc.getCurrentSampleRate();
    if (fs != regionRate)
    {
        regionRate = fs;
        for (auto& r : regions) r.valid = false;
    }
    const bool running = proc.isAudioRunning();
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bands[b];
        auto& r = regions[(size_t) b];
        if (! p.used) { r.valid = false; continue; }
        // Live (smoothed + modulated) frequency and Q while audio runs.
        const double f = running ? juce::jlimit (10.0, 30000.0, (double) tel[b].freq) : p.freq;
        const double q = running ? juce::jlimit (0.1, 20.0, (double) tel[b].q) : p.q;
        const double key[5] { (double) p.shape, f, q, (double) p.slope, fs };
        if (r.valid && std::memcmp (key, r.key, sizeof (key)) == 0) continue;
        std::memcpy (r.key, key, sizeof (key));
        r.valid = true;

        if (p.shape == Shape::Full)
        {
            r.mag.fill (1.0f);
            continue;
        }
        const auto a = dsp::designRegionFilter (p.shape, f, q, p.slope, fs);
        for (int i = 0; i < kSpecPoints; ++i)
            r.mag[(size_t) i] = (float) juce::jlimit (0.0, 1.0, a.mag (dsp::kTwoPi * pointFreq (i) / fs));
    }
}

void DistDisplay::updateSelectedCurves()
{
    const double fs = proc.getCurrentSampleRate();
    if (selected < 0) { filterCurve.valid = detCurve.valid = false; return; }
    const auto& p = bands[selected];

    if (p.filterType != BandFilter::Off)
    {
        const double key[6] { (double) p.filterType, (double) p.filterSlope, p.filterFreq, p.filterRes, p.filterGainDb, fs + selected };
        if (! filterCurve.valid || std::memcmp (key, filterCurve.key, sizeof (key)) != 0)
        {
            std::memcpy (filterCurve.key, key, sizeof (key));
            filterCurve.valid = true;
            dsp::DesignParams d;
            d.type = bandFilterDesignType (p.filterType);
            d.freq = p.filterFreq;
            d.q = p.filterRes;
            d.gainDb = p.filterGainDb;
            d.slopeIdx = regionSlopeDesignIdx (p.filterSlope);
            const auto a = dsp::designAnalog (d, fs);
            for (int i = 0; i < kSpecPoints; ++i)
                filterCurve.v[(size_t) i] = (float) dsp::gainToDb (a.mag (dsp::kTwoPi * std::min (pointFreq (i), 0.499 * fs) / fs));
        }
    }
    else filterCurve.valid = false;

    if (p.dynOn)
    {
        const auto dp = detectorDesign (p);
        const double key[6] { (double) dp.type, dp.freq, dp.q, fs, (double) selected, (double) p.detMode };
        if (! detCurve.valid || std::memcmp (key, detCurve.key, sizeof (key)) != 0)
        {
            std::memcpy (detCurve.key, key, sizeof (key));
            detCurve.valid = true;
            const auto a = dsp::designDetectorFilter (dp, fs);
            float mx = 1.0e-9f;
            for (int i = 0; i < kSpecPoints; ++i)
            {
                detCurve.v[(size_t) i] = (float) a.mag (dsp::kTwoPi * std::min (pointFreq (i), 0.499 * fs) / fs);
                mx = std::max (mx, detCurve.v[(size_t) i]);
            }
            for (auto& v : detCurve.v) v = juce::jlimit (0.0f, 1.0f, v / mx);
        }
    }
    else detCurve.valid = false;
}

void DistDisplay::updateMixAndClashes()
{
    int solo = -1;
    for (int b = 0; b < kNumBands; ++b) if (bands[b].used && bands[b].solo) { solo = b; break; }

    struct Lit { int band; float r, g, b, inten, act, drive; };
    Lit lit[kNumBands];
    int numLit = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bands[b];
        if (! p.used || ! p.enabled || ! regions[(size_t) b].valid) continue;
        if (solo >= 0 && solo != b) continue;
        const auto c = intensityColour (b, intensity[(size_t) b]);
        lit[numLit++] = { b, c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), intensity[(size_t) b], activity[(size_t) b],
                          std::max (0.5f, liveDrive[(size_t) b]) };
    }
    for (auto& row : clash) row.fill (0.0f);

    for (int i = 0; i < kSpecPoints; ++i)
    {
        float sw = 0.0f, sr = 0.0f, sg = 0.0f, sb = 0.0f, si = 0.0f, top = 0.0f;
        float c1 = 0.0f, c2 = 0.0f, h1 = 0.0f, h2 = 0.0f;
        int i1 = -1, i2 = -1;
        for (int k = 0; k < numLit; ++k)
        {
            const auto& l = lit[k];
            const float m = regions[(size_t) l.band].mag[(size_t) i];
            const float h = l.drive * m;
            if (h < 0.25f) continue;
            const float w = h * (0.4f + 0.6f * l.inten);
            sw += w; sr += w * l.r; sg += w * l.g; sb += w * l.b; si += w * l.inten;
            top = std::max (top, h);
            // Clash weight: how strongly this band works on this frequency right now.
            const float c = m * l.inten * (0.55f + 0.45f * l.act);
            if (c > c1)      { c2 = c1; i2 = i1; h2 = h1; c1 = c; i1 = l.band; h1 = h; }
            else if (c > c2) { c2 = c; i2 = l.band; h2 = h; }
        }
        if (sw > 0.0f)
        {
            mixColour[(size_t) i] = juce::Colour::fromFloatRGBA (sr / sw, sg / sw, sb / sw, 1.0f);
            mixAlpha[(size_t) i] = 0.10f + 0.30f * (si / sw);
        }
        mixTop[(size_t) i] = top;
        const float amt = i2 >= 0 ? juce::jlimit (0.0f, 1.0f, (c2 - 0.2f) / 0.45f) : 0.0f;
        clashAmt[(size_t) i] = amt;
        clashTop[(size_t) i] = std::min (h1, h2);
        if (amt > 0.0f)
        {
            clash[(size_t) i1][(size_t) i2] = std::max (clash[(size_t) i1][(size_t) i2], amt);
            clash[(size_t) i2][(size_t) i1] = clash[(size_t) i1][(size_t) i2];
        }
    }
}

// ---------------------------------------------------------------------------
void DistDisplay::paintGrid (juce::Graphics& g)
{
    const float top = plot.getY(), bottom = plot.getBottom();
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * m;
            if (f > kDispMaxF) break;
            g.setColour (m == 1 ? Colours::gridStrong : Colours::grid);
            g.drawVerticalLine ((int) std::round (freqToX (f)), top, bottom);
        }
    g.setFont (LookAndFeel::font (11.0f));
    g.setColour (Colours::textDim);
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        g.drawText (shortFreq (f), juce::Rectangle<float> (freqToX (f) - 20.0f, bottom - 16.0f, 40.0f, 14.0f), juce::Justification::centred);

    for (double d = 0.0; d <= kMaxDriveDb; d += 12.0)
    {
        const float y = driveToY (d);
        g.setColour (d == 0.0 ? Colours::gridStrong : Colours::grid);
        g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());
        g.setColour (Colours::textDim);
        const bool topLine = d + 12.0 > kMaxDriveDb;   // no room above the top line: label below it
        const float ly = topLine ? y + 2.0f : y - 14.0f;
        g.drawText (juce::String ((int) d) + " dB", juce::Rectangle<float> (plot.getX() + 4.0f, ly, 50.0f, 13.0f), juce::Justification::centredLeft);
        if (topLine)
        {
            g.setColour (Colours::textDim.withAlpha (0.7f));
            g.drawText ("DRIVE", juce::Rectangle<float> (plot.getX() + 44.0f, ly, 60.0f, 13.0f), juce::Justification::centredLeft);
        }
    }
}

void DistDisplay::paintSpectrum (juce::Graphics& g, const float* lv, juce::Colour c, float alpha)
{
    juce::Path p;
    for (int i = 0; i < kSpecPoints; ++i)
    {
        const float x = freqToX (pointFreq (i)), y = specToY (lv[i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    juce::Path f (p);
    f.lineTo (plot.getRight(), plot.getBottom());
    f.lineTo (plot.getX(), plot.getBottom());
    f.closeSubPath();
    g.setColour (c.withAlpha (0.33f * alpha));
    g.fillPath (f);
    g.setColour (c.withAlpha (0.7f * alpha));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void DistDisplay::paintBands (juce::Graphics& g, int solo)
{
    const float base = driveToY (0.0);

    // Whole-pixel columns: translucent segments must neither overlap nor leave gaps.
    auto column = [this] (int i, int& x0, int& w)
    {
        x0 = (int) std::lround (freqToX (pointFreq (i)));
        w = (int) std::lround (freqToX (pointFreq (i + 1))) - x0;
    };

    // 1. Mixed colour fill: where bands overlap their colours blend, weighted by height and intensity.
    for (int i = 0; i + 1 < kSpecPoints; ++i)
    {
        int x0, w;
        column (i, x0, w);
        if (w <= 0 || mixTop[(size_t) i] < 0.25f) continue;
        const float y = driveToY (mixTop[(size_t) i]);
        g.setColour (mixColour[(size_t) i].withAlpha (mixAlpha[(size_t) i]));
        g.fillRect (juce::Rectangle<float> ((float) x0, y, (float) w, base - y));
    }

    // 2. Clashes: red where two active bands compete, plus a marker strip below the 0 dB line.
    const float pulse = 0.75f + 0.25f * (float) std::sin (animTime * 5.0);
    for (int i = 0; i + 1 < kSpecPoints; ++i)
    {
        const float a = clashAmt[(size_t) i];
        int x0, w;
        column (i, x0, w);
        if (w <= 0 || a <= 0.0f) continue;
        const float y = driveToY (clashTop[(size_t) i]);
        g.setColour (Colours::collision.withAlpha (0.42f * a * pulse));
        g.fillRect (juce::Rectangle<float> ((float) x0, y, (float) w, base - y));
        g.setColour (Colours::collision.withAlpha (0.55f + 0.45f * a));
        g.fillRect (juce::Rectangle<float> ((float) x0, base + 2.0f, (float) w, 3.0f + 2.0f * a));
    }

    // 3. Outlines with intensity colour and a glow that follows the live output level.
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = bands[b];
            if (! p.used || ! regions[(size_t) b].valid) continue;
            const bool emph = b == selected || b == hover.band;
            if ((pass == 1) != emph) continue;
            const auto& r = regions[(size_t) b];
            const float drive = std::max (0.5f, liveDrive[(size_t) b]);
            juce::Path path;
            path.startNewSubPath (plot.getX(), base);
            for (int i = 0; i < kSpecPoints; ++i)
                path.lineTo (freqToX (pointFreq (i)), driveToY (drive * r.mag[(size_t) i]));
            path.lineTo (plot.getRight(), base);

            const float inten = intensity[(size_t) b], act = activity[(size_t) b];
            const auto col = intensityColour (b, inten);
            const float dim = (solo >= 0 && solo != b) ? 0.3f : (p.enabled ? 1.0f : 0.45f);
            if (emph)
            {
                juce::Path fill (path);
                fill.closeSubPath();
                g.setColour (col.withAlpha (0.16f * dim));
                g.fillPath (fill);
            }
            const float glow = act * (0.3f + 0.7f * inten) * dim;
            if (glow > 0.03f)
            {
                g.setColour (col.withAlpha (0.10f * glow));
                g.strokePath (path, juce::PathStrokeType (10.0f * glow + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (col.withAlpha (0.22f * glow));
                g.strokePath (path, juce::PathStrokeType (4.0f * glow + 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (col.withAlpha ((emph ? 1.0f : 0.55f + 0.4f * inten) * dim));
            g.strokePath (path, juce::PathStrokeType ((emph ? 1.6f : 1.0f) + 1.2f * inten));
        }
}

void DistDisplay::paintSelectedExtras (juce::Graphics& g)
{
    if (selected < 0) return;
    const auto& p = bands[selected];
    const auto col = bandColour (selected);
    g.setFont (LookAndFeel::font (10.0f, true));

    // Filter response (its own dB scale: 0 dB at the middle line, +-30 dB)
    if (filterCurve.valid && p.filterType != BandFilter::Off)
    {
        const float mid = filterDbToY (0.0);
        const float dash[] = { 4.0f, 4.0f };
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawDashedLine (juce::Line<float> (plot.getX(), mid, plot.getRight(), mid), dash, 2, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText ("FILTER 0 dB", juce::Rectangle<float> (plot.getRight() - 90.0f, mid - 14.0f, 84.0f, 12.0f), juce::Justification::centredRight);

        juce::Path c;
        for (int i = 0; i < kSpecPoints; ++i)
        {
            const float x = freqToX (pointFreq (i)), y = filterDbToY (filterCurve.v[(size_t) i]);
            if (i == 0) c.startNewSubPath (x, y); else c.lineTo (x, y);
        }
        juce::Path fill (c);
        fill.lineTo (plot.getRight(), mid);
        fill.lineTo (plot.getX(), mid);
        fill.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillPath (fill);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (c, juce::PathStrokeType (1.5f));

        const auto h = filterHandlePosition (selected);
        juce::Path d;
        d.addQuadrilateral (h.x, h.y - kHandleR - 1.5f, h.x + kHandleR + 1.5f, h.y, h.x, h.y + kHandleR + 1.5f, h.x - kHandleR - 1.5f, h.y);
        const bool hot = hover.what == Handle::Filter || drag.what == Handle::Filter;
        g.setColour (hot ? juce::Colours::white : juce::Colours::white.withAlpha (0.85f));
        g.fillPath (d);
        g.setColour (col);
        g.strokePath (d, juce::PathStrokeType (1.5f));
        g.setColour (juce::Colours::black);
        g.drawText ("F", juce::Rectangle<float> (h.x - 6.0f, h.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred);
    }

    // Dynamics detector: bell up to the threshold, threshold handle and the live detector level
    if (detCurve.valid && p.dynOn)
    {
        const auto h = detectorHandlePosition (selected);
        const float bottom = plot.getBottom();
        juce::Path bell;
        for (int i = 0; i < kSpecPoints; ++i)
        {
            const float x = freqToX (pointFreq (i));
            const float y = h.y + (1.0f - detCurve.v[(size_t) i]) * (bottom - h.y);
            if (i == 0) bell.startNewSubPath (x, y); else bell.lineTo (x, y);
        }
        juce::Path fill (bell);
        fill.lineTo (plot.getRight(), bottom);
        fill.lineTo (plot.getX(), bottom);
        fill.closeSubPath();
        g.setColour (Colours::warn.withAlpha (0.07f));
        g.fillPath (fill);
        juce::Path dashed;
        const float dash[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, bell, dash, 2);
        g.setColour (Colours::warn.withAlpha (0.75f));
        g.fillPath (dashed);

        // threshold line where the bell is within 6 dB of its peak
        float x0 = h.x, x1 = h.x;
        for (int i = 0; i < kSpecPoints; ++i)
            if (detCurve.v[(size_t) i] > 0.5f) { x0 = std::min (x0, freqToX (pointFreq (i))); x1 = std::max (x1, freqToX (pointFreq (i))); }
        g.setColour (Colours::warn.withAlpha (0.8f));
        g.drawLine (x0, h.y, x1, h.y, 1.2f);

        // live detector level (triangle), orange when above the threshold
        const float lvl = tel[selected].detDb;
        if (lvl > -99.0f && proc.isAudioRunning())
        {
            const float ly = specToY (lvl);
            const bool over = lvl > (float) p.dynThresh;
            juce::Path tri;
            tri.addTriangle (h.x + 12.0f, ly, h.x + 20.0f, ly - 5.0f, h.x + 20.0f, ly + 5.0f);
            g.setColour (over ? Colours::warn : juce::Colours::white.withAlpha (0.7f));
            g.fillPath (tri);
        }

        const bool hot = hover.what == Handle::Detector || drag.what == Handle::Detector;
        g.setColour (hot ? Colours::warn : Colours::warn.withAlpha (0.85f));
        g.fillEllipse (h.x - kHandleR, h.y - kHandleR, kHandleR * 2.0f, kHandleR * 2.0f);
        g.setColour (juce::Colours::black);
        g.drawText ("T", juce::Rectangle<float> (h.x - 6.0f, h.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred);
    }
}

void DistDisplay::paintNodes (juce::Graphics& g, int solo)
{
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = bands[b];
            if (! p.used) continue;
            if ((pass == 1) != (b == selected)) continue;
            const auto pos = nodePosition (b);
            const float inten = intensity[(size_t) b], act = activity[(size_t) b];
            const auto col = intensityColour (b, inten);
            const float r = kNodeR * (b == hover.band && hover.what == Handle::Node ? 1.18f : 1.0f);
            const float dim = (solo >= 0 && solo != b) ? 0.4f : 1.0f;

            // glow with the live output level
            const float glow = act * (0.35f + 0.65f * inten) * dim;
            if (glow > 0.03f && p.enabled)
            {
                const float gr = r * (1.6f + 2.6f * glow);
                juce::ColourGradient grad (col.withAlpha (0.55f * glow), pos.x, pos.y, col.withAlpha (0.0f), pos.x + gr, pos.y, true);
                g.setGradientFill (grad);
                g.fillEllipse (pos.x - gr, pos.y - gr, gr * 2.0f, gr * 2.0f);
            }

            // live drive (dynamics / modulation)
            const float ly = driveToY (liveDrive[(size_t) b]);
            if (std::abs (ly - pos.y) > 1.5f)
            {
                g.setColour (col.withAlpha (0.9f * dim));
                g.drawEllipse (pos.x - 4.5f, ly - 4.5f, 9.0f, 9.0f, 1.6f);
                g.drawLine (pos.x, pos.y, pos.x, ly, 1.0f);
            }
            if (b == selected)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawEllipse (pos.x - r - 3.0f, pos.y - r - 3.0f, (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f, 1.5f);
            }
            if (p.enabled)
            {
                g.setColour (col.withAlpha (dim));
                g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
                g.setColour (juce::Colours::black.withAlpha (0.8f));
            }
            else
            {
                g.setColour (Colours::bg2);
                g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
                g.setColour (col.withAlpha (0.8f));
                g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.5f);
            }
            g.setFont (LookAndFeel::font (10.5f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f), juce::Justification::centred);
            if (p.solo || p.mute)
            {
                g.setColour (p.solo ? Colours::warn : Colours::collision);
                g.setFont (LookAndFeel::font (10.0f, true));
                g.drawText (p.solo ? "SOLO" : "MUTE", juce::Rectangle<float> (pos.x - 20.0f, pos.y - r - 15.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
        }
}

void DistDisplay::paintReadout (juce::Graphics& g)
{
    const Hit h = drag.what != Handle::None ? drag : hover;
    g.setFont (LookAndFeel::font (12.0f));
    juce::StringArray lines;
    juce::Point<float> at;
    int colourBand = -1;
    if (h.band >= 0 && bands[h.band].used)
    {
        const auto& p = bands[h.band];
        colourBand = h.band;
        if (h.what == Handle::Filter)
        {
            at = filterHandlePosition (h.band);
            lines.add ("Filter: " + bandFilterNames()[(int) p.filterType] + "  " + filterSlopeNames()[p.filterSlope]);
            juce::String l = formatFrequency (p.filterFreq) + "   reso " + juce::String (p.filterRes, 2);
            if (filterHasGain (p.filterType)) l << "   " << formatDb (p.filterGainDb);
            lines.add (l);
        }
        else if (h.what == Handle::Detector)
        {
            at = detectorHandlePosition (h.band);
            lines.add (p.detMode == DetMode::Bell ? "Detector bell: " + formatFrequency (p.detFreq) + "  Q " + juce::String (p.detQ, 2)
                                                  : juce::String ("Detector: band region"));
            juce::String l = "threshold " + formatDb (p.dynThresh, false);
            if (proc.isAudioRunning() && tel[h.band].detDb > -99.0f) l << "   level " << formatDb (tel[h.band].detDb, false);
            lines.add (l);
        }
        else
        {
            at = nodePosition (h.band);
            juce::String l1 = distTypeNames()[(int) p.type] + "  |  " + shapeNames()[(int) p.shape];
            if (p.fxType != FxType::Off) l1 << "  |  " << fxTypeNames()[(int) p.fxType];
            lines.add (l1);
            lines.add (formatFrequency (p.freq) + "   drive " + formatDb (liveDrive[(size_t) h.band], false)
                       + "   wet " + formatNumber ((proc.isAudioRunning() ? tel[h.band].wet : (float) p.wet) * 100.0f, 0) + " %");
            int worst = -1;
            float amt = 0.05f;
            for (int o = 0; o < kNumBands; ++o)
                if (clash[(size_t) h.band][(size_t) o] > amt) { amt = clash[(size_t) h.band][(size_t) o]; worst = o; }
            if (worst >= 0) lines.add ("clashes with band " + juce::String (worst + 1) + " (" + formatNumber (amt * 100.0f, 0) + " %)");
        }
    }

    if (! lines.isEmpty())
    {
        const float hgt = 8.0f + 16.0f * (float) lines.size();
        auto box = juce::Rectangle<float> (at.x + 14.0f, at.y - hgt - 8.0f, 250.0f, hgt);
        if (box.getRight() > plot.getRight() - 4.0f) box.setX (at.x - 264.0f);
        if (box.getY() < plot.getY() + 4.0f) box.setY (at.y + 14.0f);
        g.setColour (Colours::bg2.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (bandColour (colourBand).withAlpha (0.7f));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        auto t = box.reduced (8.0f, 4.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setColour (lines[i].startsWith ("clashes") ? Colours::collision : Colours::text);
            g.drawText (lines[i], t.removeFromTop (16.0f), juce::Justification::centredLeft);
        }
    }
    else if (mouseInside)
    {
        g.setColour (Colours::textDim);
        g.drawText (formatFrequency (xToFreq (mousePos.x)) + "   " + noteNameForFrequency (xToFreq (mousePos.x))
                        + "   drive " + formatDb (yToDrive (mousePos.y), false) + "      double-click: new band",
                    juce::Rectangle<float> (plot.getRight() - 470.0f, plot.getY() + 4.0f, 462.0f, 16.0f), juce::Justification::centredRight);
    }
}

void DistDisplay::paint (juce::Graphics& g)
{
    {
        juce::ColourGradient bg (Colours::bg1, 0.0f, plot.getY(), Colours::bg0, 0.0f, plot.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRect (getLocalBounds());
    }
    paintGrid (g);
    const auto& ui = proc.uiState;
    if ((bool) ui.getProperty ("anaPre", true))  paintSpectrum (g, pre.levels(), Colours::preSpec, 0.9f);
    if ((bool) ui.getProperty ("anaPost", true)) paintSpectrum (g, post.levels(), Colours::postSpec, 0.6f);

    int solo = -1;
    for (int b = 0; b < kNumBands; ++b) if (bands[b].used && bands[b].solo) { solo = b; break; }
    paintBands (g, solo);
    paintSelectedExtras (g);
    paintNodes (g, solo);
    paintReadout (g);
}

// ---------------------------------------------------------------------------
DistDisplay::Hit DistDisplay::hitTest (juce::Point<float> p) const
{
    // Handles of the selected band first: they are small and drawn on top.
    if (selected >= 0 && bands[selected].used)
    {
        const auto& s = bands[selected];
        if (s.filterType != BandFilter::Off && filterHandlePosition (selected).getDistanceFrom (p) <= kHandleR + 3.0f)
            return { Handle::Filter, selected };
        if (s.dynOn && detectorHandlePosition (selected).getDistanceFrom (p) <= kHandleR + 3.0f)
            return { Handle::Detector, selected };
    }
    int best = -1;
    float bestD = kNodeR + 4.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! bands[b].used) continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestD || (b == selected && d <= bestD + 0.5f && d < kNodeR + 4.0f)) { bestD = d; best = b; }
    }
    return best >= 0 ? Hit { Handle::Node, best } : Hit {};
}

void DistDisplay::selectBand (int b)
{
    if (b >= 0 && ! proc.refs.readBand (b).used) b = -1;
    if (b == selected) return;
    selected = b;
    filterCurve.valid = detCurve.valid = false;
    if (b >= 0) bands[b] = proc.refs.readBand (b);
    updateSelectedCurves();
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void DistDisplay::setBandValue (int b, int index, float v)
{
    if (auto* prm = proc.param (bandParamId (b, index)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void DistDisplay::beginGesture (int b, std::initializer_list<int> params)
{
    endGesture();
    for (int i : params)
        if (auto* prm = proc.param (bandParamId (b, i)))
        {
            prm->beginChangeGesture();
            gestureParams.push_back (prm);
        }
}

void DistDisplay::endGesture()
{
    for (auto* prm : gestureParams) prm->endChangeGesture();
    gestureParams.clear();
}

void DistDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    hover = hitTest (e.position);
    setMouseCursor (hover.what != Handle::None ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void DistDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hover = {};
}

void DistDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mousePos = dragStart = e.position;
    const Hit h = hitTest (e.position);

    if (e.mods.isPopupMenu())
    {
        if (h.band >= 0) { selectBand (h.band); showBandMenu (h.band); }
        else showBackgroundMenu (e.position);
        return;
    }
    if (h.what == Handle::None)
    {
        selectBand (-1);
        return;
    }
    const auto p = proc.refs.readBand (h.band);
    if (h.what == Handle::Node && e.mods.isAltDown())
    {
        proc.deleteBand (h.band);
        selectBand (-1);
        notifyEdit();
        return;
    }
    selectBand (h.band);
    drag = h;
    switch (h.what)
    {
        case Handle::Node:
            startFreq = p.freq; startDrive = p.driveDb; startQ = p.q;
            beginGesture (h.band, { bpFreq, bpDrive, bpQ });
            break;
        case Handle::Filter:
            startFreq = p.filterFreq; startRes = p.filterRes; startGain = p.filterGainDb;
            beginGesture (h.band, { bpFilterFreq, bpFilterRes, bpFilterGain });
            break;
        case Handle::Detector:
            startFreq = p.detFreq; startThresh = p.dynThresh;
            beginGesture (h.band, { bpDetFreq, bpDynThresh });
            break;
        case Handle::None:
        default:
            break;
    }
}

void DistDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (drag.what == Handle::None) return;
    const int b = drag.band;
    const float k = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const float dx = (e.position.x - dragStart.x) * k, dy = (e.position.y - dragStart.y) * k;
    const auto p = proc.refs.readBand (b);
    switch (drag.what)
    {
        case Handle::Node:
            setBandValue (b, bpFreq, (float) juce::jlimit (10.0, 30000.0, xToFreq (freqToX (startFreq) + dx)));
            if (e.mods.isCtrlDown())
                setBandValue (b, bpQ, (float) juce::jlimit (0.1, 20.0, startQ * std::pow (2.0, -dy / 40.0)));
            else
                setBandValue (b, bpDrive, (float) yToDrive (driveToY (startDrive) + dy));
            break;
        case Handle::Filter:
            setBandValue (b, bpFilterFreq, (float) juce::jlimit (20.0, 20000.0, xToFreq (freqToX (startFreq) + dx)));
            if (filterHasGain (p.filterType) && ! e.mods.isCtrlDown())
                setBandValue (b, bpFilterGain, (float) juce::jlimit (-24.0, 24.0, yToFilterDb (filterDbToY (startGain) + dy)));
            else
                setBandValue (b, bpFilterRes, (float) juce::jlimit (0.1, 10.0, startRes * std::pow (2.0, -dy / 40.0)));
            break;
        case Handle::Detector:
            if (p.detMode == DetMode::Bell)
                setBandValue (b, bpDetFreq, (float) juce::jlimit (20.0, 20000.0, xToFreq (freqToX (startFreq) + dx)));
            setBandValue (b, bpDynThresh, (float) juce::jlimit (-80.0, 6.0, yToSpec (specToY ((float) startThresh) + dy)));
            break;
        case Handle::None:
        default:
            break;
    }
}

void DistDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag.what != Handle::None)
    {
        endGesture();
        drag = {};
        notifyEdit();
    }
}

void DistDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const Hit h = hitTest (e.position);
    if (h.what == Handle::Node)
    {
        proc.setParam (bandParamId (h.band, bpEnabled), proc.refs.readBand (h.band).enabled ? 0.0f : 1.0f);
        notifyEdit();
        return;
    }
    if (h.what != Handle::None) return;   // handles: no double-click action
    createBandAt (e.position, Shape::Bell);
}

void DistDisplay::createBandAt (juce::Point<float> pos, Shape shape)
{
    const int b = proc.createBand (shape, xToFreq (pos.x), yToDrive (pos.y), 1.0, DistType::Tube);
    if (b >= 0)
    {
        bands[b] = proc.refs.readBand (b);
        selectBand (b);
        notifyEdit();
    }
}

void DistDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const Hit h = hitTest (e.position);
    if (h.what == Handle::None) return;
    float delta = w.deltaY;
    if (w.isReversed) delta = -delta;
    const double step = delta * (e.mods.isShiftDown() ? 0.4 : 2.0);
    const auto p = proc.refs.readBand (h.band);
    switch (h.what)
    {
        case Handle::Filter:
            proc.setParam (bandParamId (h.band, bpFilterRes), (float) juce::jlimit (0.1, 10.0, p.filterRes * std::pow (2.0, step)));
            break;
        case Handle::Detector:
            if (p.detMode == DetMode::Bell)
                proc.setParam (bandParamId (h.band, bpDetQ), (float) juce::jlimit (0.1, 20.0, p.detQ * std::pow (2.0, step)));
            break;
        case Handle::Node:
            if (e.mods.isCtrlDown())
                proc.setParam (bandParamId (h.band, bpWet), (float) juce::jlimit (0.0, 100.0, p.wet * 100.0 + delta * 40.0));
            else
                proc.setParam (bandParamId (h.band, bpQ), (float) juce::jlimit (0.1, 20.0, p.q * std::pow (2.0, step)));
            break;
        case Handle::None:
        default:
            break;
    }
    wheelCommit = 20;
}

bool DistDisplay::keyPressed (const juce::KeyPress& key)
{
    if (selected < 0) return false;
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        proc.deleteBand (selected);
        selectBand (-1);
        notifyEdit();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) { selectBand (-1); return true; }
    const int code = key.getKeyCode();
    const bool fine = key.getModifiers().isShiftDown();
    const auto p = proc.refs.readBand (selected);
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const double step = (code == juce::KeyPress::rightKey ? 1.0 : -1.0) / (fine ? 48.0 : 12.0);
        proc.setParam (bandParamId (selected, bpFreq), (float) juce::jlimit (10.0, 30000.0, p.freq * std::pow (2.0, step)));
        notifyEdit();
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const double dir = code == juce::KeyPress::upKey ? 1.0 : -1.0;
        proc.setParam (bandParamId (selected, bpDrive), (float) juce::jlimit (0.0, 48.0, p.driveDb + dir * (fine ? 0.1 : 1.0)));
        notifyEdit();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
void DistDisplay::showBandMenu (int b)
{
    const auto p = proc.refs.readBand (b);
    juce::PopupMenu m, shapes, types, filters, fxs, det;
    const auto sn = shapeNames(), tn = distTypeNames(), fn = bandFilterNames(), xn = fxTypeNames(), dn = detModeNames();
    for (int i = 0; i < sn.size(); ++i) shapes.addItem (100 + i, sn[i], true, (int) p.shape == i);
    for (int i = 0; i < tn.size(); ++i) types.addItem (200 + i, tn[i], true, (int) p.type == i);
    for (int i = 0; i < fn.size(); ++i) filters.addItem (300 + i, fn[i], true, (int) p.filterType == i);
    for (int i = 0; i < xn.size(); ++i) fxs.addItem (400 + i, xn[i], true, (int) p.fxType == i);
    for (int i = 0; i < dn.size(); ++i) det.addItem (500 + i, dn[i], p.dynOn, (int) p.detMode == i);
    m.addSectionHeader ("Band " + juce::String (b + 1));
    m.addSubMenu ("Shape", shapes);
    m.addSubMenu ("Distortion", types);
    m.addSubMenu ("Filter", filters);
    m.addSubMenu ("FX", fxs);
    m.addSeparator();
    m.addItem (1, "Enabled", true, p.enabled);
    m.addItem (2, "Solo", true, p.solo);
    m.addItem (6, "Mute", true, p.mute);
    m.addItem (3, "Dynamics", true, p.dynOn);
    m.addSubMenu ("Dynamics Detector", det, p.dynOn);
    m.addItem (7, "Reset Effect Order", p.chainCode != 0);
    m.addSeparator();
    m.addItem (4, "Reset Band");
    m.addItem (5, "Delete Band");

    juce::Component::SafePointer<DistDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, b, p] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        if (r >= 100 && r < 200) pr.setParam (bandParamId (b, bpShape), (float) (r - 100));
        if (r >= 200 && r < 300) pr.setParam (bandParamId (b, bpType), (float) (r - 200));
        if (r >= 300 && r < 400) pr.setParam (bandParamId (b, bpFilterType), (float) (r - 300));
        if (r >= 400 && r < 500) pr.setParam (bandParamId (b, bpFxType), (float) (r - 400));
        if (r >= 500 && r < 600) pr.setParam (bandParamId (b, bpDetMode), (float) (r - 500));
        if (r == 1) pr.setParam (bandParamId (b, bpEnabled), p.enabled ? 0.0f : 1.0f);
        if (r == 2)
        {
            const bool solo = ! p.solo;
            pr.clearAllSolos();
            if (solo) pr.setParam (bandParamId (b, bpSolo), 1.0f);
        }
        if (r == 3) pr.setParam (bandParamId (b, bpDynOn), p.dynOn ? 0.0f : 1.0f);
        if (r == 6) pr.setParam (bandParamId (b, bpMute), p.mute ? 0.0f : 1.0f);
        if (r == 7) pr.setParam (bandParamId (b, bpChainOrder), 0.0f);
        if (r == 4)
        {
            const float f = pr.getParam (bandParamId (b, bpFreq));
            pr.resetBand (b);
            pr.setParam (bandParamId (b, bpFreq), f);
            pr.setParam (bandParamId (b, bpDetFreq), juce::jlimit (20.0f, 20000.0f, f));
        }
        if (r == 5) { pr.deleteBand (b); safe->selectBand (-1); }
        safe->notifyEdit();
    });
}

void DistDisplay::showBackgroundMenu (juce::Point<float> pos)
{
    juce::PopupMenu m;
    const auto sn = shapeNames();
    for (int i = 0; i < sn.size(); ++i)
        m.addItem (100 + i, "Add " + sn[i] + " Band Here", proc.findFreeBand() >= 0);
    m.addSeparator();
    m.addItem (1, "Delete All Bands");
    juce::Component::SafePointer<DistDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, pos] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 100) { safe->createBandAt (pos, (Shape) (r - 100)); return; }
        if (r == 1)
        {
            for (int b = 0; b < kNumBands; ++b) if (safe->proc.refs.readBand (b).used) safe->proc.deleteBand (b);
            safe->selectBand (-1);
            safe->notifyEdit();
        }
    });
}

} // namespace gkd::ui
