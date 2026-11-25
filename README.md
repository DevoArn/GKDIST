# GKDIST

A free, open-source 24-band multiband distortion plug-in for Windows (VST3,
x64), by GK. Every band has its own distortion (10 types), filter, Phat
(sub-octave weight), noise, compressor and clipper, plus dynamics that can move
the band's drive, wet amount and gain with the level of the input or an
external side-chain. The global section adds high/low-pass filters, a bus
compressor, noise, a clipper, a look-ahead brick-wall limiter and dry/wet mix.
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
  below), *High* (everything above) or *Full* (whole signal); slopes 12, 24
  or 48 dB/oct.
- Enable, Solo (listen to the processed region only), Delete.

**Distortion** (per band) – Soft Clip, Hard Clip, Tube, Tape, Transistor,
Diode, Fuzz, Foldback, Rectify, Bitcrush. Drive 0 – 48 dB, Bias (asymmetry –
even harmonics; sample-rate reduction for Bitcrush), Auto Gain (keeps the
level constant when the drive changes). A live transfer curve shows the shape.

**Per-band modules**
- *Filter* – low-pass, high-pass or band-pass with resonance, before or after
  the distortion.
- *Phat* – sub-octave generator and low-end warmth for weight and fatness.
- *Noise* – white, pink or crackle, shaped to the band's region; *Follow*
  makes it track the signal envelope (100 % = silent when the input is).
- *Compressor* – threshold, ratio, attack, release, makeup, with a
  gain-reduction meter.
- *Clip / Limit* – soft clip, hard clip or limit at an adjustable ceiling.
- *Output* – Wet (dry/wet of the band) and Gain.

**Dynamics** (per band) – threshold, attack, release, internal or external
side-chain detection. Above the threshold the band's **gain** (±24 dB),
**drive** (±48 dB) and **wet** (±100 %) move by their ranges – e.g. distort
only the loud hits, or add grit only when the signal is quiet. The live drive
movement is shown as a ring on the node, the amount as a meter.

**Global section** – oversampling (Off, 2x, 4x, 8x, linear-phase with exact
integer latency), high-pass / low-pass (12 or 24 dB/oct), bus compressor,
noise, clipper (soft / hard), brick-wall limiter with 1 ms look-ahead (no
sample exceeds the ceiling), dry/wet mix (latency-compensated), output gain,
click-free latency-compensated bypass.

**Workflow** – undo/redo, A/B with copy, 10 factory presets and user presets
(saved to `Documents\GKDIST\Presets`), pre / post spectrum analyser
(range, resolution, speed, tilt and freeze in *Settings*), resizable window, GPU (OpenGL) drawing (toggle in *Settings*),
context menus, all 911 parameters automatable. Type, shape and slope changes
are click-free (the band fades out and back in over 5 ms).

### Mouse & keyboard

| Action | Result |
|---|---|
| Double-click empty area | New band (Bell) at that frequency and drive |
| Drag node | Frequency + drive |
| Ctrl+drag / Shift+drag | Adjust Q / fine adjustment |
| Wheel on node / Ctrl+wheel | Q (Shift: fine) / wet |
| Double-click node / Alt+click node | Enable-bypass / delete |
| Right-click node / empty area | Band menu (shape, type, solo, dynamics …) / add band, delete all |
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
- CPU, stereo 48 kHz: 4 bands at 4x 3 % of one core; 24 bands with every
  module on at 4x 35 %.
- VST3 host test: loads, side-chain bus, 911 parameters, latency switching,
  transparent with no bands, hard clipping produces odd harmonics with auto
  gain holding the level, limiter ceiling, state round trip.
- UI test: node drag and double-click create/edit the right parameters, undo,
  hostile / corrupt state blobs are rejected or clamped.

## Known limitations

- No side-chain spectrum in the analyser, no mid/side processing and no
  surround support.
- Oversampling uses linear-phase filters: with 2x – 8x there are 71 – 81
  samples of extra latency at 48 kHz, and content above ~20 kHz (at 44.1 /
  48 kHz) is rolled off by the half-band filters.
- 24 bands with every module enabled at 8x oversampling needs a large part
  of one CPU core; use 2x or 4x for many bands.
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
