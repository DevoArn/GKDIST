// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DistDisplay.h"
#include <cstring>

namespace gkd::ui
{

namespace
{
    constexpr float kNodeR = 7.5f;
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
}

DistDisplay::DistDisplay (GKDistProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
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

juce::Point<float> DistDisplay::nodePosition (int b) const
{
    return { freqToX (bands[b].freq), driveToY (bands[b].driveDb) };
}

// ---------------------------------------------------------------------------
void DistDisplay::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.001, 0.1, (now - lastTime) * 0.001);
    lastTime = now;

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

    for (int b = 0; b < kNumBands; ++b)
        bands[b] = proc.refs.readBand (b);
    if (selected >= 0 && ! bands[selected].used)
        selectBand (-1);
    updateRegions();

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
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& p = bands[b];
        auto& r = regions[(size_t) b];
        if (! p.used) { r.valid = false; continue; }
        const double key[5] { (double) p.shape, p.freq, p.q, (double) p.slope, fs };
        if (r.valid && std::memcmp (key, r.key, sizeof (key)) == 0) continue;
        std::memcpy (r.key, key, sizeof (key));
        r.valid = true;

        if (p.shape == Shape::Full)
        {
            r.mag.fill (1.0f);
            continue;
        }
        dsp::DesignParams d;
        d.freq = p.freq;
        d.slopeIdx = regionSlopeDesignIdx (p.slope);
        d.type = p.shape == Shape::Bell ? FilterType::BandPass : (p.shape == Shape::Low ? FilterType::HighCut : FilterType::LowCut);
        d.q = p.shape == Shape::Bell ? p.q : kDefaultQ;
        const auto a = dsp::designAnalog (d, fs);
        for (int i = 0; i < kSpecPoints; ++i)
            r.mag[(size_t) i] = (float) juce::jlimit (0.0, 1.0, a.mag (dsp::kTwoPi * pointFreq (i) / fs));
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

    // Band regions: height = drive * region magnitude
    const float base = driveToY (0.0);
    int solo = -1;
    for (int b = 0; b < kNumBands; ++b) if (bands[b].used && bands[b].solo) { solo = b; break; }
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = bands[b];
            if (! p.used || ! regions[(size_t) b].valid) continue;
            const bool emph = b == selected || b == hover;
            if ((pass == 1) != emph) continue;
            const auto& r = regions[(size_t) b];
            const double drive = std::max (0.5, p.driveDb);
            juce::Path path;
            path.startNewSubPath (plot.getX(), base);
            for (int i = 0; i < kSpecPoints; ++i)
                path.lineTo (freqToX (pointFreq (i)), driveToY (drive * r.mag[(size_t) i]));
            path.lineTo (plot.getRight(), base);
            path.closeSubPath();
            const auto col = bandColour (b);
            const float dim = (solo >= 0 && solo != b) ? 0.35f : (p.enabled ? 1.0f : 0.4f);
            g.setColour (col.withAlpha ((emph ? 0.30f : 0.14f) * dim));
            g.fillPath (path);
            g.setColour (col.withAlpha ((emph ? 0.95f : 0.55f) * dim));
            g.strokePath (path, juce::PathStrokeType (emph ? 1.6f : 1.0f));
        }

    // Nodes
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& p = bands[b];
            if (! p.used) continue;
            if ((pass == 1) != (b == selected)) continue;
            const auto pos = nodePosition (b);
            const auto col = bandColour (b);
            const float r = kNodeR * (b == hover ? 1.18f : 1.0f);
            const float dim = (solo >= 0 && solo != b) ? 0.4f : 1.0f;

            if (p.dynOn)
            {
                const double live = juce::jlimit (0.0, kMaxDriveDb, p.driveDb + p.dynDrive * proc.getDynAmount (b));
                const float ly = driveToY (live);
                if (std::abs (ly - pos.y) > 1.0f)
                {
                    g.setColour (col.withAlpha (0.9f * dim));
                    g.drawEllipse (pos.x - 4.5f, ly - 4.5f, 9.0f, 9.0f, 1.6f);
                    g.drawLine (pos.x, pos.y, pos.x, ly, 1.0f);
                }
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
            if (p.solo)
            {
                g.setColour (Colours::warn);
                g.setFont (LookAndFeel::font (10.0f, true));
                g.drawText ("SOLO", juce::Rectangle<float> (pos.x - 20.0f, pos.y - r - 15.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
        }

    // Readout
    const int rb = dragBand >= 0 ? dragBand : hover;
    g.setFont (LookAndFeel::font (12.0f));
    if (rb >= 0 && bands[rb].used)
    {
        const auto& p = bands[rb];
        const auto pos = nodePosition (rb);
        juce::String l1 = distTypeNames()[(int) p.type] + "  |  " + shapeNames()[(int) p.shape];
        juce::String l2 = formatFrequency (p.freq) + "   drive " + formatDb (p.driveDb, false) + "   wet " + juce::String (p.wet * 100.0, 0) + " %";
        auto box = juce::Rectangle<float> (pos.x + 14.0f, pos.y - 46.0f, 230.0f, 38.0f);
        if (box.getRight() > plot.getRight() - 4.0f) box.setX (pos.x - 244.0f);
        if (box.getY() < plot.getY() + 4.0f) box.setY (pos.y + 14.0f);
        g.setColour (Colours::bg2.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (bandColour (rb).withAlpha (0.7f));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        g.setColour (Colours::text);
        auto t = box.reduced (8.0f, 3.0f);
        g.drawText (l1, t.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.drawText (l2, t, juce::Justification::centredLeft);
    }
    else if (mouseInside)
    {
        g.setColour (Colours::textDim);
        g.drawText (formatFrequency (xToFreq (mousePos.x)) + "   " + noteNameForFrequency (xToFreq (mousePos.x))
                        + "   drive " + formatDb (yToDrive (mousePos.y), false) + "      double-click: new band",
                    juce::Rectangle<float> (plot.getRight() - 470.0f, plot.getY() + 4.0f, 462.0f, 16.0f), juce::Justification::centredRight);
    }
}

// ---------------------------------------------------------------------------
int DistDisplay::hitTest (juce::Point<float> p) const
{
    int best = -1;
    float bestD = kNodeR + 4.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! bands[b].used) continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestD || (b == selected && d <= bestD + 0.5f && d < kNodeR + 4.0f)) { bestD = d; best = b; }
    }
    return best;
}

void DistDisplay::selectBand (int b)
{
    if (b >= 0 && ! proc.refs.readBand (b).used) b = -1;
    if (b == selected) return;
    selected = b;
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void DistDisplay::setBandValue (int b, int index, float v)
{
    if (auto* prm = proc.param (bandParamId (b, index)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void DistDisplay::beginGesture (int b)
{
    for (int i : { (int) bpFreq, (int) bpDrive, (int) bpQ })
        if (auto* prm = proc.param (bandParamId (b, i))) prm->beginChangeGesture();
}

void DistDisplay::endGesture (int b)
{
    for (int i : { (int) bpFreq, (int) bpDrive, (int) bpQ })
        if (auto* prm = proc.param (bandParamId (b, i))) prm->endChangeGesture();
}

void DistDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    hover = hitTest (e.position);
    setMouseCursor (hover >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void DistDisplay::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hover = -1;
}

void DistDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mousePos = dragStart = e.position;
    const int hb = hitTest (e.position);

    if (e.mods.isPopupMenu())
    {
        if (hb >= 0) { selectBand (hb); showBandMenu (hb); }
        else showBackgroundMenu (e.position);
        return;
    }
    if (hb < 0)
    {
        selectBand (-1);
        return;
    }
    if (e.mods.isAltDown())
    {
        proc.deleteBand (hb);
        selectBand (-1);
        notifyEdit();
        return;
    }
    selectBand (hb);
    dragBand = hb;
    const auto p = proc.refs.readBand (hb);
    startFreq = p.freq; startDrive = p.driveDb; startQ = p.q;
    beginGesture (hb);
}

void DistDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragBand < 0) return;
    const float k = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const float dx = (e.position.x - dragStart.x) * k, dy = (e.position.y - dragStart.y) * k;
    setBandValue (dragBand, bpFreq, (float) juce::jlimit (10.0, 30000.0, xToFreq (freqToX (startFreq) + dx)));
    if (e.mods.isCtrlDown())
        setBandValue (dragBand, bpQ, (float) juce::jlimit (0.1, 20.0, startQ * std::pow (2.0, -dy / 40.0)));
    else
        setBandValue (dragBand, bpDrive, (float) yToDrive (driveToY (startDrive) + dy));
}

void DistDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
    {
        endGesture (dragBand);
        dragBand = -1;
        notifyEdit();
    }
}

void DistDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int hb = hitTest (e.position);
    if (hb >= 0)
    {
        proc.setParam (bandParamId (hb, bpEnabled), proc.refs.readBand (hb).enabled ? 0.0f : 1.0f);
        notifyEdit();
        return;
    }
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
    const int hb = hitTest (e.position);
    if (hb < 0) return;
    float delta = w.deltaY;
    if (w.isReversed) delta = -delta;
    const auto p = proc.refs.readBand (hb);
    if (e.mods.isCtrlDown())
        proc.setParam (bandParamId (hb, bpWet), (float) juce::jlimit (0.0, 100.0, p.wet * 100.0 + delta * 40.0));
    else
        proc.setParam (bandParamId (hb, bpQ), (float) juce::jlimit (0.1, 20.0, p.q * std::pow (2.0, delta * (e.mods.isShiftDown() ? 0.4 : 2.0))));
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
    juce::PopupMenu m, shapes, types;
    const auto sn = shapeNames(), tn = distTypeNames();
    for (int i = 0; i < sn.size(); ++i) shapes.addItem (100 + i, sn[i], true, (int) p.shape == i);
    for (int i = 0; i < tn.size(); ++i) types.addItem (200 + i, tn[i], true, (int) p.type == i);
    m.addSectionHeader ("Band " + juce::String (b + 1));
    m.addSubMenu ("Shape", shapes);
    m.addSubMenu ("Distortion", types);
    m.addSeparator();
    m.addItem (1, "Enabled", true, p.enabled);
    m.addItem (2, "Solo", true, p.solo);
    m.addItem (3, "Dynamics", true, p.dynOn);
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
        if (r == 1) pr.setParam (bandParamId (b, bpEnabled), p.enabled ? 0.0f : 1.0f);
        if (r == 2)
        {
            const bool solo = ! p.solo;
            pr.clearAllSolos();
            if (solo) pr.setParam (bandParamId (b, bpSolo), 1.0f);
        }
        if (r == 3) pr.setParam (bandParamId (b, bpDynOn), p.dynOn ? 0.0f : 1.0f);
        if (r == 4)
        {
            const float f = pr.getParam (bandParamId (b, bpFreq));
            pr.resetBand (b);
            pr.setParam (bandParamId (b, bpFreq), f);
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
