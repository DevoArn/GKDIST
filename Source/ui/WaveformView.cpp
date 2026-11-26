// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WaveformView.h"

namespace gkd::ui
{

namespace
{
    constexpr float kZooms[] = { 1.0f, 2.0f, 4.0f, 8.0f };
    const double kLogRange = std::log (WaveformView::kMinSeconds / WaveformView::kMaxSeconds);

    // speed slider 0..1: 0 = 20 s window (slow), 1 = 0.25 s (fast)
    double secondsFromSpeed (double v) { return WaveformView::kMaxSeconds * std::exp (juce::jlimit (0.0, 1.0, v) * kLogRange); }
    double speedFromSeconds (double s) { return std::log (juce::jlimit (WaveformView::kMinSeconds, WaveformView::kMaxSeconds, s) / WaveformView::kMaxSeconds) / kLogRange; }

    juce::String secondsText (double s) { return formatNumber (s, s < 1.0 ? 2 : (s < 10.0 ? 1 : 0)) + " s"; }
}

WaveformView::WaveformView (GKDistProcessor& p) : proc (p), hist (p.waveHistory)
{
    scratch.resize (2048);
    setOpaque (true);
    speed.setSliderStyle (juce::Slider::LinearHorizontal);
    speed.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    speed.setRange (0.0, 1.0);
    speed.setValue (speedFromSeconds (getSeconds()), juce::dontSendNotification);
    speed.setDoubleClickReturnValue (true, speedFromSeconds (2.0));
    speed.setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.6f));
    speed.setColour (juce::Slider::thumbColourId, Colours::accent);
    speed.setTooltip ("Scrolling speed: the time shown across the view (right = faster). Ctrl+wheel on the waveform also works.");
    speed.onValueChange = [this] { setSeconds (secondsFromSpeed (speed.getValue())); };
    addAndMakeVisible (speed);
    head = (double) hist.total;
}

WaveformView::~WaveformView() = default;

double WaveformView::getSeconds() const
{
    const double s = (double) proc.uiState.getProperty ("waveSeconds", 2.0);
    return juce::jlimit (kMinSeconds, kMaxSeconds, std::isfinite (s) ? s : 2.0);
}

void WaveformView::setSeconds (double s)
{
    s = juce::jlimit (kMinSeconds, kMaxSeconds, s);
    proc.uiState.setProperty ("waveSeconds", s, nullptr);
    const double v = speedFromSeconds (s);
    if (std::abs (speed.getValue() - v) > 1.0e-6) speed.setValue (v, juce::dontSendNotification);
    repaint();
}

void WaveformView::resizeHistory()
{
    hist.rate = proc.getCurrentSampleRate();
    const int cols = (int) std::ceil (kMaxSeconds * hist.rate / dsp::DistEngine::kWaveColumnSamples) + 1;
    hist.columns.assign ((size_t) juce::jmax (16, cols), dsp::WaveColumn());
    hist.writePos = hist.count = 0;
    hist.total = 0;
    head = 0.0;
}

void WaveformView::pull()
{
    proc.requestWaveform();
    if (hist.columns.empty() || proc.getCurrentSampleRate() != hist.rate) resizeHistory();
    const int size = (int) hist.columns.size();
    int got = 0;
    for (;;)
    {
        const int n = proc.readWaveform (scratch.data(), (int) scratch.size());
        if (n <= 0) break;
        got += n;
        for (int i = 0; i < n; ++i)
        {
            hist.columns[(size_t) hist.writePos] = scratch[(size_t) i];
            hist.writePos = (hist.writePos + 1) % size;
            hist.count = juce::jmin (hist.count + 1, size);
            ++hist.total;
        }
    }
    if (got > 0) burst += 0.1 * ((double) got - burst);
}

void WaveformView::frame (double dt)
{
    if (! isVisible()) return;
    pull();
    if (frozen) return;   // the FIFO keeps draining, the picture stands still

    const double total = (double) hist.total;
    if (! proc.isAudioRunning())
    {
        head = total;   // audio stopped: rest at the end
    }
    else
    {
        // Audio arrives in host blocks; scroll at real time and stay a little
        // behind the newest column so the motion is continuous.
        const double cps = columnsPerSecond();
        const double desired = total - juce::jlimit (2.0, juce::jmax (2.0, cps * 0.1), burst * 1.5);
        head += dt * cps;
        const double err = desired - head;
        if (std::abs (err) > cps * 0.25) head = desired;                  // start / resync
        else head += err * (1.0 - std::exp (-dt / 0.3));
        head = juce::jmin (head, total);
    }
    if (budget.due (dt)) repaint();
}

void WaveformView::resized()
{
    auto header = getLocalBounds().removeFromTop (22).reduced (10, 2);
    header.removeFromLeft (520);
    speedLabel = header.removeFromLeft (52);
    speed.setBounds (header.removeFromLeft (130).reduced (0, 1));
    speedValue = header.removeFromLeft (52);
}

juce::Rectangle<float> WaveformView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (22.0f).reduced (8.0f, 3.0f);
}

WaveformView::Pixel WaveformView::aggregate (int x, int width) const
{
    Pixel px;
    if (hist.count == 0 || width <= 0 || hist.columns.empty()) return px;
    const double visible = getSeconds() * columnsPerSecond();
    const long long size = (long long) hist.columns.size();
    const long long oldest = hist.total - hist.count;
    // pixel x covers column time [t0, t1); the right edge is the (fractional) head
    const double t0 = head - visible + visible * x / width, t1 = head - visible + visible * (x + 1) / width;
    const long long c0 = (long long) std::floor (t0);
    const long long c1 = juce::jmax (c0 + 1, (long long) std::ceil (t1));
    for (long long c = c0; c < c1; ++c)
    {
        if (c < oldest || c >= hist.total || c < 0) continue;
        const auto& col = hist.columns[(size_t) (c % size)];
        if (px.n == 0) { px.inMin = col.inMin; px.inMax = col.inMax; px.outMin = col.outMin; px.outMax = col.outMax; }
        else
        {
            px.inMin = juce::jmin (px.inMin, col.inMin); px.inMax = juce::jmax (px.inMax, col.inMax);
            px.outMin = juce::jmin (px.outMin, col.outMin); px.outMax = juce::jmax (px.outMax, col.outMax);
        }
        for (int b = 0; b < kNumBands; ++b) { px.effect[b] += col.effect[b]; px.total += col.effect[b]; }
        ++px.n;
    }
    return px;
}

int WaveformView::dominantBand (float x, float* share) const
{
    const auto plot = plotArea();
    const int w = (int) plot.getWidth();
    const int ix = juce::jlimit (0, juce::jmax (0, w - 1), (int) (x - plot.getX()));
    float eff[kNumBands] {};
    float total = 0.0f;
    for (int d = -2; d <= 2; ++d)
    {
        if (ix + d < 0 || ix + d >= w) continue;
        const auto px = aggregate (ix + d, w);
        for (int b = 0; b < kNumBands; ++b) { eff[b] += px.effect[b]; total += px.effect[b]; }
    }
    int best = -1;
    float bestV = 0.0f;
    for (int b = 0; b < kNumBands; ++b) if (eff[b] > bestV) { bestV = eff[b]; best = b; }
    if (share != nullptr) *share = total > 0.0f ? bestV / total : 0.0f;
    return total > 1.0e-6f ? best : -1;
}

void WaveformView::paint (juce::Graphics& g)
{
    struct Record { PaintBudget& b; double t0; ~Record() { b.record (juce::Time::getMillisecondCounterHiRes() - t0); } }
        record { budget, juce::Time::getMillisecondCounterHiRes() };
    const auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient bg (Colours::bg1, 0.0f, 0.0f, Colours::bg0, 0.0f, bounds.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (bounds);
    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (0, 0.0f, bounds.getRight());

    const auto plot = plotArea();
    const float mid = plot.getCentreY(), half = 0.5f * plot.getHeight();
    const float zoom = kZooms[juce::jlimit (0, 3, (int) proc.uiState.getProperty ("waveZoom", 0))];
    auto yOf = [&] (float v) { return mid - juce::jlimit (-1.0f, 1.0f, v * zoom) * half; };

    // guides: 0 and half scale
    g.setColour (Colours::gridStrong);
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    g.setColour (Colours::grid);
    for (float v : { -0.5f, 0.5f }) g.drawHorizontalLine ((int) (mid - v * half), plot.getX(), plot.getRight());

    const int w = (int) plot.getWidth();
    float bandTotals[kNumBands] {};
    if (hist.count > 0)
    {
        std::array<juce::Colour, kNumBands> cols;
        for (int b = 0; b < kNumBands; ++b) cols[(size_t) b] = bandColour (b);
        const auto neutral = Colours::textDim.withAlpha (0.9f);
        const auto inputColour = Colours::text.withAlpha (0.13f);
        for (int x = 0; x < w; ++x)
        {
            const auto px = aggregate (x, w);
            if (px.n == 0) continue;
            const float fx = plot.getX() + (float) x;
            // input, behind
            g.setColour (inputColour);
            const float iy0 = yOf (px.inMax), iy1 = yOf (px.inMin);
            g.fillRect (fx, iy0, 1.0f, juce::jmax (1.0f, iy1 - iy0));

            // output, tinted by the bands that change it
            juce::Colour c = neutral;
            if (px.total > 1.0e-7f)
            {
                float r = 0, gg = 0, bb = 0;
                for (int b = 0; b < kNumBands; ++b)
                {
                    const float wgt = px.effect[b] / px.total;
                    if (wgt <= 0.0f) continue;
                    r += wgt * cols[(size_t) b].getFloatRed(); gg += wgt * cols[(size_t) b].getFloatGreen(); bb += wgt * cols[(size_t) b].getFloatBlue();
                    bandTotals[b] += px.effect[b];
                }
                // how much of the output is the bands' doing (change RMS vs. output peak)
                const float amp = juce::jmax (std::abs (px.outMin), std::abs (px.outMax));
                const float strength = juce::jlimit (0.0f, 1.0f, (px.total / (float) px.n) / (0.35f * amp + 1.0e-4f));
                c = neutral.interpolatedWith (juce::Colour::fromFloatRGBA (r, gg, bb, 1.0f), 0.25f + 0.75f * strength);
            }
            const float oy0 = yOf (px.outMax), oy1 = yOf (px.outMin);
            g.setColour (c);
            g.fillRect (fx, oy0, 1.0f, juce::jmax (1.0f, oy1 - oy0));
        }
    }

    // header: title, legend, speed, the most active bands in view
    g.setFont (LookAndFeel::font (10.5f, true));
    g.setColour (Colours::textDim);
    g.drawText ("WAVEFORM", 10, 4, 90, 14, juce::Justification::centredLeft);
    g.setFont (LookAndFeel::font (10.5f));
    g.drawText (juce::String ("grey: input   colour: output, tinted by the bands that change it")
                    + (zoom > 1.0f ? "   zoom x" + formatNumber (zoom, 0) : juce::String()) + (frozen ? "   FROZEN" : ""),
                100, 4, 420, 14, juce::Justification::centredLeft);
    g.setFont (LookAndFeel::font (10.5f, true));
    g.drawText ("SPEED", speedLabel, juce::Justification::centredRight);
    g.setColour (Colours::text);
    g.setFont (LookAndFeel::font (10.5f));
    g.drawText (secondsText (getSeconds()), speedValue, juce::Justification::centredLeft);
    {
        int order[kNumBands];
        for (int b = 0; b < kNumBands; ++b) order[b] = b;
        std::sort (order, order + kNumBands, [&bandTotals] (int a, int b) { return bandTotals[a] > bandTotals[b]; });
        float sum = 0.0f;
        for (float v : bandTotals) sum += v;
        float x = bounds.getRight() - 10.0f;
        const float minX = (float) speedValue.getRight() + 8.0f;
        for (int i = 0; i < juce::jmin (5, kNumBands); ++i)
        {
            const int b = order[i];
            if (sum <= 0.0f || bandTotals[b] / sum < 0.02f) break;
            const juce::String t = juce::String (b + 1) + "  " + formatNumber (100.0f * bandTotals[b] / sum, 0) + " %";
            const float tw = (float) LookAndFeel::font (10.5f).getStringWidth (t) + 16.0f;
            if (x - tw < minX) break;
            x -= tw;
            g.setColour (bandColour (b));
            g.fillRoundedRectangle (x, 7.0f, 9.0f, 9.0f, 2.0f);
            g.setColour (Colours::text);
            g.drawText (t, juce::Rectangle<float> (x + 12.0f, 4.0f, tw - 12.0f, 14.0f), juce::Justification::centredLeft);
            x -= 6.0f;
        }
    }

    if (hist.count == 0)
    {
        g.setColour (Colours::textDim);
        g.setFont (LookAndFeel::font (12.0f));
        g.drawText ("waiting for audio...", plot, juce::Justification::centred);
    }

    // hover: time and the band that dominates there
    if (mouseX >= plot.getX() && mouseX < plot.getRight() && hist.count > 0)
    {
        g.setColour (Colours::text.withAlpha (0.4f));
        g.drawVerticalLine ((int) mouseX, plot.getY(), plot.getBottom());
        float share = 0.0f;
        const int b = dominantBand (mouseX, &share);
        const double secs = getSeconds() * (1.0 - (mouseX - plot.getX()) / plot.getWidth());
        juce::String t = "-" + juce::String (secs, 2) + " s";
        if (b >= 0) t << "   band " << (b + 1) << " (" << formatNumber (share * 100.0f, 0) << " % of the change)";
        g.setFont (LookAndFeel::font (11.0f));
        auto box = juce::Rectangle<float> (mouseX + 8.0f, plot.getY() + 2.0f, 280.0f, 18.0f);
        if (box.getRight() > plot.getRight()) box.setX (mouseX - 288.0f);
        g.setColour (Colours::bg2.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (b >= 0 ? bandColour (b) : Colours::text);
        g.drawText (t, box.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e) { mouseX = e.position.x; }
void WaveformView::mouseExit (const juce::MouseEvent&) { mouseX = -1.0f; }

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showMenu();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent&) { frozen = ! frozen; repaint(); }

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float dir = (w.isReversed ? -w.deltaY : w.deltaY) > 0.0f ? 1.0f : -1.0f;
    if (e.mods.isCtrlDown())
    {
        setSeconds (getSeconds() * std::pow (2.0, -0.25 * dir));   // up = faster
        return;
    }
    const int z = juce::jlimit (0, 3, (int) proc.uiState.getProperty ("waveZoom", 0) + (int) dir);
    proc.uiState.setProperty ("waveZoom", z, nullptr);
    repaint();
}

void WaveformView::showMenu()
{
    juce::PopupMenu m, span, zoom;
    static const double secs[] = { 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0 };
    const double cur = getSeconds();
    for (int i = 0; i < 7; ++i) span.addItem (10 + i, secondsText (secs[i]), true, std::abs (cur - secs[i]) < 1.0e-3);
    const int z = (int) proc.uiState.getProperty ("waveZoom", 0);
    for (int i = 0; i < 4; ++i) zoom.addItem (20 + i, "x" + formatNumber (kZooms[i], 0), true, z == i);
    m.addSubMenu ("Time Shown (speed)", span);
    m.addSubMenu ("Zoom", zoom);
    m.addItem (1, "Freeze", true, frozen);
    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->frozen = ! safe->frozen;
        if (r >= 10 && r < 17) safe->setSeconds (secs[r - 10]);
        if (r >= 20 && r < 24) safe->proc.uiState.setProperty ("waveZoom", r - 20, nullptr);
        safe->repaint();
    });
}

} // namespace gkd::ui
