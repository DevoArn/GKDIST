# GKDIST

A free, open-source 24-band multiband distortion plug-in for Windows (VST3,
x64), by GK. Every band has its own effect chain - distortion (10 types),
filter, Phat (sub-octave weight), noise, modulation FX (phaser, flanger,
chorus, vibrato, tremolo, auto pan), compressor and clipper - in an order you
set by dragging. Per-band dynamics move drive, wet and gain with the level in
an independent detector bell (or an external side-chain), and a modulation
matrix routes two LFOs and two envelope followers to any band parameter.
A mixer page, intensity-coloured bands and clash marking show what each band
is doing. The global section adds high/low-pass filters, a bus compressor,
noise, a clipper, a look-ahead brick-wall limiter and dry/wet mix.
Works in any VST3 host (DAW) on Windows; a standalone app is included.

It has its own name and visual design and is not affiliated with any
commercial product.

---

## Install

1. Download **`GKDIST 1.0.0 Setup.exe`** from the
   [Releases](https://github.com/DevoArn/GKDIST/releases) page (or build it, see below)
   and run it (accept the Windows admin prompt). Choose the components (VST3
   plug-in, standalone app, Start Menu shortcuts) and press *Install*.
2. Rescan plug-ins in your DAW. Most hosts scan the standard VST3 folder
   automatically; some need it enabled in their plug-in settings.
3. GKDIST appears with the maker name **GK** in the *Distortion* category.

| Installed to | |
|---|---|
| `C:\Program Files\Common Files\VST3\GKDIST.vst3` | VST3 plug-in (folder selectable) |
| `C:\Program Files\GKDIST\` | Standalone app, uninstaller, licence and notices |
| Start Menu → *GKDIST* | Shortcuts to the app and the uninstaller |
| `Documents\GKDIST\Presets` | Your presets (created on first save, kept on uninstall unless you tick the option) |
| `%APPDATA%\GKDIST\GKDIST.settings` | Your colour theme (shared by all instances) |

Uninstall from *Settings → Apps → Installed apps → GKDIST* or the Start Menu.
The installer can run while a DAW is open: a plug-in file the host has loaded
is moved aside and the new version is used after the host restarts. Silent use:
`"GKDIST 1.0.0 Setup.exe" /S` and `"Uninstall GKDIST.exe" /S`.

Side-chain: route a side-chain signal to GKDIST's second input in your host,
then enable *EXT SC* in a band's *Dynamics* section.

## Build

Clone with the dependencies: `git clone --recursive https://github.com/DevoArn/GKDIST.git`
(or run `git submodule update --init --depth 1`; `build.bat` also does this
automatically on a fresh clone).

Requirements: Windows 10/11 x64 and Visual Studio 2019 or 2022 (any edition,
or the Build Tools) with the *Desktop development with C++* workload;
`build.bat` finds it automatically and uses its bundled CMake and Ninja.
Python 3 is needed for `build.bat installer` only.

```
build.bat            Release VST3 + Standalone        -> build\GKDIST_artefacts\Release\
build.bat installer  Release build + installer        -> dist\GKDIST 1.0.0 Setup.exe
build.bat tests      DSP test-suite (latency, null tests, aliasing, dynamics, modules, CPU)
build.bat hosttest   Loads the built VST3 in a test host (params, latency, distortion, limiter, state)
build.bat uitest     In-process UI test: renders build\ui_*.png, simulates mouse edits, hostile state
build.bat debug      Debug build
```

Installer test (install into a temp folder, byte-compare, reinstall while the
DLL is loaded, uninstall): `powershell -ExecutionPolicy Bypass -File Tests\installer_test.ps1`
after `build.bat installer`. The installer source is in `Installer\` (a JUCE app
that embeds the build as a zip payload made by `Tools\make_payload.py`).

Dependencies live in `external\` as git submodules: JUCE 7.0.12 (`build.bat`
lowers its `cmake_minimum_required` from 3.22 to 3.20 so the CMake shipped
with VS 2019 works too) and PFFFT (FFT for the analyser).

---

## Features

**Bands** – up to 24, drawn as nodes over a live spectrum: horizontal position
is the frequency, height is the drive. Each band processes a *region* of the
spectrum and puts the result back, so a band with 0 % wet and 0 dB gain is
fully transparent, and bands can overlap and run in series.
- Region: *Bell* (band around the frequency, Q 0.1 – 20), *Low* (everything
  below), *High* (everything above) or *Full* (whole signal); slopes 12, 24,
  48 dB/oct or **Brickwall** (144 dB/oct edges for Low / High; for Bell a flat
  band between the Q edges with 96 dB/oct sides).
- Enable, Solo (listen to the processed region only), Mute (removes the
  region), Pan, Delete.

**Distortion** (per band) – Soft Clip, Hard Clip, Tube, Tape, Transistor,
Diode, Fuzz, Foldback, Rectify, Bitcrush and **Phase Dist**. Drive 0 – 48 dB,
Bias (asymmetry – even harmonics; sample-rate reduction for Bitcrush), Auto
Gain (keeps the level constant when the drive changes). A live transfer curve
shows the shape and moves with dynamics and modulation. *Phase Dist* warps the
waveform in time instead of clipping it: the band modulates its own phase
through a short delay (Depth up to 4 ms, Tone = brightness of the modulator) –
FM-like harmonics, especially rich on bass, at an unchanged level.

**Per-band effect chain** – seven modules shown as tiles in the *EFFECT CHAIN*
(each with a short summary such as "LP 5.0k" or "Fuzz 22 dB"). Click a tile
to open that module's page with its controls; drag a tile to change the order
(e.g. filter before or after the distortion, compressor before the phaser …).
Order changes are click-free. The band panel keeps region and output on the
left and dynamics on the right; chips for all 24 bands next to the band name
select any band (their colour shows how hard each band is driven).
- *Filter* – Low Pass, High Pass, Band Pass, Notch, Peak, Low Shelf, High
  Shelf, Tilt; 12 / 24 / 48 dB/oct, resonance, gain. Drawn on the display
  with its own handle.
- *Distortion* – see above.
- *Phat* – sub-octave generator and low-end warmth for weight and fatness.
- *Noise* – white, pink or crackle, shaped to the band's region; *Follow*
  makes it track the signal envelope (100 % = silent when the input is).
- *FX* – Phaser (6 stages), Flanger, Chorus, Vibrato, Tremolo, Auto Pan, with
  rate, depth, feedback and mix; stereo LFO offset.
- *Compressor* – threshold, ratio, attack, release, makeup, with a
  gain-reduction meter.
- *Clip / Limit* – soft clip, hard clip or limit at an adjustable ceiling.
- After the chain: *Wet* (dry/wet of the band), *Gain*, *Pan*, *Mute*.

**Dynamics** (per band) – threshold, attack, release, internal or external
side-chain detection. The detector listens either to the band's own region or
to an **independent bell** (frequency, Q) – like a dynamic EQ: e.g. turn the
distortion's wet up only when the 3 kHz area gets loud. Above the threshold
the band's **gain** (±24 dB), **drive** (±48 dB) and **wet** (±100 %) move by
their ranges. On the display the detector bell, its threshold handle **T**
and the live detector level are drawn; the live drive is a ring on the node.

**Mixer** (*MIXER* page) – a channel strip per band: drive, wet, pan, gain
fader with output level meter and peak hold, mute, solo.

**Modulation** (*MODULATION* page) – two LFOs (sine, triangle, saw up/down,
square, sample & hold, smooth random; free rate 0.01 – 20 Hz or tempo-synced
1/32 … 4 bars, following the host's song position), input and side-chain
envelope followers, and 8 matrix slots: source → band (one or all) → target
(drive, wet, gain, frequency, Q, bias, filter freq / reso / gain, phat,
noise, FX rate / depth / mix, pan) with amount ±100 %.

**Visual feedback** – a band's colour gets more saturated and brighter the
harder it is driven (drive × wet, including dynamics and modulation), bands
glow with their live output level, overlapping bands mix their colours, and
**clashes** – frequencies where two active bands work hard at the same time –
are marked red under the curves (the hover read-out names the competing band).

**OTT mode** of the bus compressor (*OTT* button) – 3-band upward + downward
compression: the signal is split at two Linkwitz-Riley crossovers (88 Hz and
2.5 kHz by default; the bands sum flat), and in each band loud parts are
pushed down and quiet parts pulled up, with the classic OTT-style thresholds,
ratios and times. DEPTH (dry/wet), TIME (attack / release scale), UP and
DOWN amounts, IN and OUT gain; the meter shows each band's boost / cut. The
crossovers are tabs at the top of the display: drag them (double-click =
default).

**Waveform view** (*WAVEFORM* button) – a scrolling waveform (0.25 – 20 s,
set with SPEED or Ctrl+wheel): the
input in grey behind the output, which is coloured by the bands that change
the signal at each moment, mixed by how much each band changes it. The legend
ranks the busiest bands in view; hovering names the dominant band at that
time. It scrolls smoothly at the display's refresh rate. Double-click:
freeze; wheel: zoom; right-click: speed, zoom, freeze.

**Smooth at any refresh rate** – all animation (curves, glow, meters, scopes,
waveform) runs on the vertical blank of the monitor the plug-in is on (60, 120,
144, 240 Hz …) and is time-based, so it looks the same at any rate. Curves
follow dynamics and modulation and glide between audio blocks. A view whose
drawing would take more than its share of a frame repaints every 2nd / 3rd
frame instead, so the interface stays responsive.

**Colour themes** – *Settings → Colour Theme*: Ember (warm, default), Sunset
(warm), Vintage (warm) or Midnight (cool). The interface switches at once; the
choice is remembered for all GKDIST instances.

**Global section** – oversampling (Off, 2x, 4x, 8x, linear-phase with exact
integer latency), high-pass / low-pass (12 or 24 dB/oct), bus compressor (with
an **OTT** mode, below),
noise, clipper (soft / hard), brick-wall limiter with 1 ms look-ahead (no
sample exceeds the ceiling), dry/wet mix (latency-compensated), output gain,
click-free latency-compensated bypass.

**Workflow** – undo/redo, A/B with copy, 12 factory presets and user presets
(saved to `Documents\GKDIST\Presets`), pre / post spectrum analyser
(range, resolution, speed, tilt and freeze in *Settings*), resizable window, GPU (OpenGL) drawing (toggle in *Settings*),
context menus, 1250 parameters (all automatable except the chain order).
Type, shape, slope, filter, FX and order changes are click-free (the band
fades out and back in over 5 ms).

### Mouse & keyboard

| Action | Result |
|---|---|
| Double-click empty area | New band (Bell) at that frequency and drive |
| Drag node | Frequency + drive |
| Ctrl+drag / Shift+drag | Adjust Q / fine adjustment |
| Wheel on node / Ctrl+wheel | Q (Shift: fine) / wet |
| Double-click node / Alt+click node | Enable-bypass / delete |
| Drag **F** handle (selected band) | Filter frequency + gain (resonance for LP/HP/BP/Notch; Ctrl: resonance) |
| Drag **T** handle (dynamic band) | Detector bell frequency + threshold; wheel: bell Q |
| Click / drag a module in EFFECT CHAIN | Edit that module / change the band's effect order (right-click: reset) |
| Click a band chip (next to the band name) | Select that band |
| Waveform: double-click / wheel / Ctrl+wheel / right-click | Freeze / zoom / speed / speed, zoom, freeze menu |
| Drag an **OTT** tab (top of the display, OTT on) | Move that crossover; double-click: default |
| Click / double-click a mixer strip header | Select the band / open it on the BAND page |
| Right-click node / empty area | Band menu (shape, type, filter, FX, solo, mute, dynamics, detector …) / add band, delete all |
| Delete, Esc | Delete selected band, deselect |
| Left/Right, Up/Down (Shift: fine) | Frequency ±1/12 octave, drive ±1 dB |
| Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z | Undo, redo |

---

## Verification (from `build.bat tests` / `hosttest` / `uitest`)

- Reported latency = measured latency at 44.1 / 48 / 96 kHz for every
  oversampling factor (48 kHz: 48 / 119 / 126 / 129 samples for Off / 2x /
  4x / 8x; the 1 ms limiter look-ahead is always included so the latency
  does not change when the limiter is switched).
- No bands or neutral bands: bit-exact without oversampling, null −98 … −116 dB
  with oversampling.
- Aliasing (hard clip, 7.1 kHz, 24 dB drive): strongest alias −14.5 dB without
  oversampling, −89 … −93 dB with 2x / 4x / 8x.
- Band isolation: a 3 kHz bell band leaves 100 Hz untouched (−0.00 dB).
- Dynamics: −12 dB gain range gives −11.96 dB on a loud tone; quiet signals
  stay clean when the drive is dynamic.
- Compressor −30 dB / 4:1 on a −20 dBFS tone: −27.45 dBFS (theory −27.5);
  limiter: peak exactly at the ceiling with +18 dB of hot fuzz.
- Noise levels within 0.01 dB of the setting for all three colours.
- 20 s of random automation of all 24 bands with NaN input: output finite and
  limited.
- Effect chain: all 5040 orders encode / decode; a low pass after a hard
  clipper removes the 3rd harmonic 35 dB more than one before it.
- Phase distortion: clean at 0 dB depth (harmonics below −100 dB), 2nd
  harmonic −10 dB at 24 dB and +7 dB at 48 dB, level within 1.4 dB.
- OTT: the three bands sum flat (0.000 dB, 40 Hz – 15 kHz); gain law unity at
  −24 dB per band; a −6 dB tone is pushed down 13 dB, a −50 dB tone lifted
  13 dB (44 dB of range squeezed to 18 dB); silence is not lifted; stable with
  random settings.
- Brickwall regions: Low / High −84 dB half an octave past the edge; Bell
  (Q 1) 0.00 dB in the band, below −100 dB outside; a clipping 1 kHz Low band
  leaves a 3 kHz tone untouched (harmonic −158 dB vs. −37 dB at 12 dB/oct).
- Band filter: Peak +12 dB measures +12.00 dB, Notch below −250 dB, 24 dB/oct
  low pass −48.2 dB two octaves up, High Shelf −9 dB measures −8.99 dB.
- FX: all six types active and stable with 90 % feedback; tremolo swing 61 dB;
  hard-left pan / mute remove the band exactly.
- Detector bell at 5 kHz: a loud 1 kHz tone gives amount 0.00, a 5 kHz tone
  1.00; in region mode it follows the band.
- Modulation: LFO routed to one band only (+24.00 dB at the crest), tempo-synced
  phase exact (0.2500 after a 1/16 at 120 BPM), envelope source, ±12 dB drive
  modulation measured in the engine.
- CPU, stereo 48 kHz: 4 bands at 4x 3 % of one core; 24 bands with every
  module on (incl. phaser and dynamics) at 4x about 45 %.
- VST3 host test: loads, side-chain bus, 1250 parameters, latency switching,
  transparent with no bands, hard clipping produces odd harmonics with auto
  gain holding the level, limiter ceiling, state round trip.
- UI test: dragging nodes, the filter handle, the detector handle and chain
  modules sets the right parameters; undo; intensity and clash detection;
  one mixer strip per band; LFO modulation visible in the telemetry; a click
  on a chain tile opens that module's page; the waveform attributes 100 % of
  the change to the only active band; every colour theme rebuilds the UI and
  keeps the selection; all factory presets load and process; hostile /
  corrupt state blobs are rejected or clamped.

## Known limitations

- No side-chain spectrum in the analyser, no mid/side processing and no
  surround support.
- Oversampling uses linear-phase filters: with 2x – 8x there are 71 – 81
  samples of extra latency at 48 kHz, and content above ~20 kHz (at 44.1 /
  48 kHz) is rolled off by the half-band filters.
- 24 bands with every module enabled at 8x oversampling needs most of one CPU
  core; use 2x or 4x for many bands.
- The chain order is not automatable (it is saved with the project, presets,
  undo and A/B). Modulation updates every 32 samples (0.7 ms at 48 kHz).
- The plug-in has been verified with automated tests and a JUCE test host.

## Contributing

Bug reports and pull requests are welcome, see [CONTRIBUTING.md](CONTRIBUTING.md)
and the [changelog](CHANGELOG.md).

## Licence

GKDIST is free software: you can redistribute it and/or modify it under the
terms of the **GNU General Public License version 3 or later** (see [LICENSE](LICENSE)).
Copyright (C) 2026 Roman Gutekunst (GK).
It is distributed WITHOUT ANY WARRANTY.

Third-party components:

- [JUCE](https://juce.com) 7 – GPLv3.
- VST3 SDK (included with JUCE) – Steinberg Media Technologies GmbH, used under
  GPLv3. VST is a trademark of Steinberg Media Technologies GmbH, registered in
  Europe and other countries.
- [PFFFT](https://github.com/marton78/pffft) – BSD-style licence
  (`external/pffft/LICENSE.txt`).

The installer ships `LICENSE.txt` and `Third-Party Notices.txt` (with a link to
this source code) next to the standalone app.
