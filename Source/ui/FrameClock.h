// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace gkd::ui
{

/** Drives a component's animation at the refresh rate of the display it is on
    (60, 120, 144, 240 Hz ...): the callback runs on every vertical blank and
    gets the real time since the previous frame, so all motion is time-based and
    looks the same at any refresh rate.

    Without vertical-blank events (component not on screen yet, a host without
    them, off-screen rendering in tests) a 60 Hz timer takes over. */
class FrameClock : private juce::Timer
{
public:
    FrameClock (juce::Component& c, std::function<void (double dtSeconds)> cb)
        : callback (std::move (cb)), vblank (&c, [this] { onVBlank(); })
    {
        last = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }

    ~FrameClock() override { stopTimer(); }

    /** Frames per second over the last second (for diagnostics). */
    double getFrameRate() const noexcept { return fps; }

private:
    void onVBlank()
    {
        lastVBlank = juce::Time::getMillisecondCounterHiRes();
        tick (lastVBlank);
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastVBlank > 100.0)   // no vertical blanks: fall back to the timer
            tick (now);
    }

    void tick (double now)
    {
        const double dt = juce::jlimit (0.0002, 0.1, (now - last) * 0.001);
        last = now;
        ++frames;
        if (now - fpsStart >= 1000.0)
        {
            fps = frames * 1000.0 / (now - fpsStart);
            frames = 0;
            fpsStart = now;
        }
        if (callback) callback (dt);
    }

    std::function<void (double)> callback;
    juce::VBlankAttachment vblank;
    double last = 0.0, lastVBlank = -1.0e9, fpsStart = 0.0, fps = 0.0;
    int frames = 0;
};

/** Keeps an animated view inside a share of the frame time: the view records
    how long its paint takes; if that exceeds the share, it repaints only every
    2nd, 3rd ... frame (motion stays time-based), so the UI stays responsive at
    any refresh rate. */
struct PaintBudget
{
    explicit PaintBudget (double fractionOfFrame = 0.45) : share (fractionOfFrame) {}

    double share;                          // fraction of a frame this view may use
    std::atomic<double> paintMs { 0.0 };   // smoothed paint time (paint may run on the OpenGL thread)
    int counter = 0;

    void record (double ms) noexcept
    {
        const double p = paintMs.load (std::memory_order_relaxed);
        paintMs.store (p <= 0.0 ? ms : 0.85 * p + 0.15 * ms, std::memory_order_relaxed);
    }

    /** Call once per frame with the frame interval; true = repaint this frame. */
    bool due (double dtSeconds) noexcept
    {
        const double budgetMs = share * dtSeconds * 1000.0, p = paintMs.load (std::memory_order_relaxed);
        const int every = p > budgetMs && budgetMs > 0.0 ? juce::jlimit (1, 8, (int) std::ceil (p / budgetMs)) : 1;
        if (++counter >= every) { counter = 0; return true; }
        return false;
    }
};

/** Frame-rate independent exponential smoothing: moves v towards target with
    time constant tau (seconds) over dt. */
inline float smoothTowards (float v, float target, double dt, double tau) noexcept
{
    const float a = (float) (1.0 - std::exp (-dt / std::max (1.0e-4, tau)));
    return v + a * (target - v);
}

} // namespace gkd::ui
