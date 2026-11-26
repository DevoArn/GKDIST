// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChainStrip.h"

namespace gkd::ui
{

namespace
{
    constexpr float kArrowW = 14.0f;

    juce::String shortHz (double f)
    {
        if (f >= 1000.0) return formatNumber (f / 1000.0, f >= 10000.0 ? 0 : 1) + "k";
        return formatNumber (f, 0);
    }
}

ChainStrip::ChainStrip (GKDistProcessor& p) : proc (p)
{
    setTooltip ("Effect chain of this band, signal flows left to right. Click a module to edit it, "
                "drag it to change the order; right-click to reset the order.");
}

void ChainStrip::setBand (int b)
{
    band = b;
    dragSlot = -1;
    if (band >= 0) update (proc.refs.readBand (band));
    repaint();
}

void ChainStrip::setSelectedModule (Module m)
{
    if (m == selectedModule) return;
    selectedModule = m;
    repaint();
}

void ChainStrip::update (const BandParams& p)
{
    params = p;
    if (dragSlot < 0)
        for (int i = 0; i < kNumModules; ++i) order[i] = p.order[i];
    repaint();
}

juce::Rectangle<float> ChainStrip::tileBounds (int slot) const
{
    const float w = ((float) getWidth() - kArrowW * (kNumModules - 1)) / (float) kNumModules;
    return { (float) slot * (w + kArrowW), 0.0f, w, (float) getHeight() };
}

juce::Point<float> ChainStrip::slotCentre (int slot) const { return tileBounds (slot).getCentre(); }

int ChainStrip::slotAt (float x) const
{
    const float w = ((float) getWidth() - kArrowW * (kNumModules - 1)) / (float) kNumModules;
    return juce::jlimit (0, kNumModules - 1, (int) std::floor ((x + 0.5f * kArrowW) / (w + kArrowW)));
}

bool ChainStrip::moduleActive (Module m, const BandParams& p)
{
    switch (m)
    {
        case Module::Filter:     return p.filterType != BandFilter::Off;
        case Module::Distortion: return true;
        case Module::Phat:       return p.phat > 0.0;
        case Module::Noise:      return p.noiseDb > kNoiseOffDb + 0.05;
        case Module::Fx:         return p.fxType != FxType::Off;
        case Module::Compressor: return p.compOn;
        case Module::Clipper:    return p.clipMode != ClipMode::Off;
        case Module::Count:
        default:                 return false;
    }
}

juce::String ChainStrip::moduleSummary (Module m, const BandParams& p)
{
    if (! moduleActive (m, p)) return "off";
    switch (m)
    {
        case Module::Filter:
        {
            static const char* s[] = { "", "LP", "HP", "BP", "Notch", "Peak", "LoSh", "HiSh", "Tilt" };
            return juce::String (s[(int) p.filterType]) + " " + shortHz (p.filterFreq);
        }
        case Module::Distortion:
        {
            static const char* s[] = { "Soft", "Hard", "Tube", "Tape", "Trans", "Diode", "Fuzz", "Fold", "Rect", "Crush", "Phase" };
            static_assert (sizeof (s) / sizeof (s[0]) == (size_t) DistType::Count, "summary names out of sync");
            return juce::String (s[(int) p.type]) + " " + formatNumber (p.driveDb, 0) + " dB";
        }
        case Module::Phat:       return formatNumber (p.phat * 100.0, 0) + " %";
        case Module::Noise:      return noiseTypeNames()[(int) p.noiseType] + " " + formatNumber (p.noiseDb, 0);
        case Module::Fx:         return fxTypeNames()[(int) p.fxType];
        case Module::Compressor: return formatNumber (p.compRatio, p.compRatio < 10.0 ? 1 : 0) + ":1";
        case Module::Clipper:
        {
            static const char* s[] = { "", "Soft", "Hard", "Limit" };
            return juce::String (s[(int) p.clipMode]) + " " + formatNumber (p.clipCeiling, 0);
        }
        case Module::Count:
        default:                 return {};
    }
}

void ChainStrip::paint (juce::Graphics& g)
{
    if (band < 0) return;
    const auto names = moduleNames();
    const auto col = bandColour (band);

    auto drawTile = [&] (juce::Rectangle<float> r, int module, bool lifted)
    {
        const bool on = moduleActive ((Module) module, params);
        const bool sel = (Module) module == selectedModule;
        auto rr = r.reduced (0.5f);
        g.setColour (lifted ? Colours::bg2.brighter (0.25f) : (on ? col.withAlpha (sel ? 0.36f : 0.20f) : Colours::bg1));
        g.fillRoundedRectangle (rr, 5.0f);
        if (sel)
        {
            g.setColour (Colours::accent);
            g.drawRoundedRectangle (rr, 5.0f, 2.0f);
            g.fillRect (rr.getX() + 8.0f, rr.getBottom() - 3.0f, rr.getWidth() - 16.0f, 3.0f);
        }
        else
        {
            g.setColour (lifted ? Colours::text : (on ? col.withAlpha (0.8f) : Colours::panelEdge));
            g.drawRoundedRectangle (rr, 5.0f, lifted ? 1.6f : 1.0f);
        }
        // active dot
        g.setColour (on ? col : Colours::panelEdge);
        g.fillEllipse (rr.getX() + 6.0f, rr.getY() + 6.0f, 6.0f, 6.0f);

        auto t = rr.reduced (4.0f, 4.0f);
        g.setColour (on || sel ? Colours::text : Colours::textDim);
        g.setFont (LookAndFeel::font (11.5f, true));
        g.drawText (names[module], t.removeFromTop (t.getHeight() * 0.55f), juce::Justification::centredBottom);
        g.setColour (on ? Colours::text.withAlpha (0.75f) : Colours::textDim.withAlpha (0.6f));
        g.setFont (LookAndFeel::font (10.0f));
        g.drawFittedText (moduleSummary ((Module) module, params), t.toNearestInt(), juce::Justification::centredTop, 1, 0.8f);
    };

    for (int s = 0; s < kNumModules; ++s)
    {
        const auto r = tileBounds (s);
        if (s < kNumModules - 1)
        {
            const float ax = r.getRight() + 3.0f, ay = r.getCentreY();
            juce::Path a;
            a.addTriangle (ax + 1.0f, ay - 4.5f, ax + kArrowW - 5.0f, ay, ax + 1.0f, ay + 4.5f);
            g.setColour (Colours::textDim);
            g.fillPath (a);
        }
        if (s == dragSlot && moved)
        {
            g.setColour (Colours::panelEdge);
            const float dash[] = { 3.0f, 3.0f };
            auto rr = r.reduced (1.0f);
            g.drawDashedLine ({ rr.getX(), rr.getY(), rr.getRight(), rr.getY() }, dash, 2);
            g.drawDashedLine ({ rr.getX(), rr.getBottom(), rr.getRight(), rr.getBottom() }, dash, 2);
            continue;
        }
        drawTile (r, order[s], false);
    }
    if (dragSlot >= 0 && moved)
    {
        auto r = tileBounds (dragSlot);
        r.setX (juce::jlimit (0.0f, (float) getWidth() - r.getWidth(), dragX - grabOffset));
        drawTile (r, order[dragSlot], true);
    }
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    if (band < 0) return;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Reset Order (Filter > Drive > Phat > Noise > FX > Comp > Clip)", params.chainCode != 0);
        juce::Component::SafePointer<ChainStrip> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
        {
            if (safe == nullptr || r != 1 || safe->band < 0) return;
            for (int i = 0; i < kNumModules; ++i) safe->order[i] = i;
            safe->commitOrder();
        });
        return;
    }
    dragSlot = slotAt (e.position.x);
    grabOffset = e.position.x - tileBounds (dragSlot).getX();
    dragX = e.position.x;
    moved = false;
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSlot < 0) return;
    if (! moved && e.getDistanceFromDragStart() < 4) return;
    moved = true;
    dragX = e.position.x;
    // Move the dragged module to the slot under the tile's centre.
    const float centre = dragX - grabOffset + 0.5f * tileBounds (0).getWidth();
    const int target = slotAt (centre);
    if (target != dragSlot)
    {
        const int m = order[dragSlot];
        if (target > dragSlot) for (int s = dragSlot; s < target; ++s) order[s] = order[s + 1];
        else                   for (int s = dragSlot; s > target; --s) order[s] = order[s - 1];
        order[target] = m;
        dragSlot = target;
    }
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragSlot >= 0 && moved)
        commitOrder();
    else if (dragSlot >= 0)
    {
        // A click: edit that module.
        selectedModule = (Module) order[dragSlot];
        if (onModuleSelected) onModuleSelected (selectedModule);
    }
    dragSlot = -1;
    moved = false;
    repaint();
}

void ChainStrip::commitOrder()
{
    const int code = encodeChainOrder (order);
    if (code != params.chainCode)
    {
        proc.setParam (bandParamId (band, bpChainOrder), (float) code);
        params.chainCode = code;
        if (onEdit) onEdit();
    }
    repaint();
}

} // namespace gkd::ui
