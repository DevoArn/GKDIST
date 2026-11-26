// GKDIST - 24-band multiband distortion plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChainStrip.h"

namespace gkd::ui
{

namespace
{
    constexpr float kArrowW = 12.0f;
}

ChainStrip::ChainStrip (GKDistProcessor& p) : proc (p)
{
    setTooltip ("Effect chain of this band, signal flows left to right. Drag a module to change the order; right-click to reset.");
}

void ChainStrip::setBand (int b)
{
    band = b;
    dragSlot = -1;
    if (band >= 0) update (proc.refs.readBand (band));
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

bool ChainStrip::moduleActive (Module m) const
{
    switch (m)
    {
        case Module::Filter:     return params.filterType != BandFilter::Off;
        case Module::Distortion: return true;
        case Module::Phat:       return params.phat > 0.0;
        case Module::Noise:      return params.noiseDb > kNoiseOffDb + 0.05;
        case Module::Fx:         return params.fxType != FxType::Off;
        case Module::Compressor: return params.compOn;
        case Module::Clipper:    return params.clipMode != ClipMode::Off;
        case Module::Count:
        default:                 return false;
    }
}

void ChainStrip::paint (juce::Graphics& g)
{
    if (band < 0) return;
    const auto names = moduleNames();
    const auto col = bandColour (band);
    g.setFont (LookAndFeel::font (11.0f, true));

    auto drawTile = [&] (juce::Rectangle<float> r, int module, bool lifted)
    {
        const bool on = moduleActive ((Module) module);
        g.setColour (lifted ? Colours::bg2.brighter (0.25f) : (on ? col.withAlpha (0.28f) : Colours::bg1));
        g.fillRoundedRectangle (r.reduced (0.5f), 4.0f);
        g.setColour (lifted ? juce::Colours::white : (on ? col : Colours::panelEdge));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, lifted ? 1.6f : 1.0f);
        g.setColour (on ? Colours::text : Colours::textDim);
        g.drawText (names[module], r, juce::Justification::centred);
    };

    for (int s = 0; s < kNumModules; ++s)
    {
        const auto r = tileBounds (s);
        if (s < kNumModules - 1)
        {
            const float ax = r.getRight() + 2.0f, ay = r.getCentreY();
            juce::Path a;
            a.addTriangle (ax + 2.0f, ay - 4.0f, ax + kArrowW - 4.0f, ay, ax + 2.0f, ay + 4.0f);
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
    if (dragSlot >= 0 && moved) commitOrder();
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
