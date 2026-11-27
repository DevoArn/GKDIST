# GKDIST
<img width="1600" height="1040" alt="ui_large" src="https://github.com/user-attachments/assets/d5f482a4-1591-424d-b7a0-9cc55551cc7e" />

24-band multiband distortion, VST3 + standalone, Windows x64. GPLv3.

## Install

Run `GKDIST x.y.z Setup.exe` from [Releases](https://github.com/DevoArn/GKDIST/releases), rescan plug-ins (maker **GK**, category *Distortion*). Silent: `/S`.

| Path | Content |
|---|---|
| `C:\Program Files\Common Files\VST3\GKDIST.vst3` | Plug-in |
| `C:\Program Files\GKDIST\` | Standalone, uninstaller, licences |
| `Documents\GKDIST\Presets` | User presets |
| `%APPDATA%\GKDIST\GKDIST.settings` | Colour theme |

Upgrades work while a DAW has the plug-in loaded (old file moved aside, new one used after restart).

## Build

Windows 10/11 x64, VS 2019/2022 (or Build Tools) with *Desktop development with C++*; Python 3 for the installer only.

```
git clone --recursive https://github.com/DevoArn/GKDIST.git
build.bat            Release VST3 + standalone  -> build\GKDIST_artefacts\Release\
build.bat installer  + installer                -> dist\GKDIST x.y.z Setup.exe
build.bat tests      DSP tests
build.bat hosttest   VST3 in a test host
build.bat uitest     UI test, renders build\ui_*.png
build.bat debug      Debug build
powershell -ExecutionPolicy Bypass -File Tests\installer_test.ps1   (after installer)
```

Submodules in `external\`: JUCE 7.0.12 (`build.bat` lowers its CMake minimum to 3.20) and PFFFT. Installer: `Installer\` (JUCE app, zip payload from `Tools\make_payload.py`). CI: `.github/workflows/ci.yml` – build + all tests on every push; a `v*` tag publishes a release.

## Features

**Bands** (up to 24): node = frequency × drive. Region Bell (Q 0.1 – 20) / Low / High / Full, 12 / 24 / 48 dB/oct or Brickwall (Low/High 144 dB/oct; Bell flat between the Q edges, 96 dB/oct sides). Model: `y = x + fade·(pan·mute·gain·(b + wet·(chain(b) − b)) − b)` – 0 % wet, 0 dB = transparent. Enable, solo, mute, pan.

**Effect chain** per band, 7 modules, order draggable (5040 orders, click-free):
- Filter: LP, HP, BP, Notch, Peak, Low/High Shelf, Tilt; 12 / 24 / 48 dB/oct, resonance, gain.
- Distortion: Soft Clip, Hard Clip, Tube, Tape, Transistor, Diode, Fuzz, Foldback, Rectify, Bitcrush, Phase Dist (self phase modulation, depth ≤ 4 ms, tone 125 Hz – 8 kHz). Drive 0 – 48 dB, bias, auto gain.
- Phat: sub-octave + low-end weight.
- Noise: white / pink / crackle, region-shaped, envelope follow.
- FX: phaser (6 stages), flanger, chorus, vibrato, tremolo, auto pan.
- Compressor, Clip / Limit.

**Dynamics** per band: detector on the region or an own bell (freq, Q), internal / external side-chain; threshold, attack, release; ranges gain ±24 dB, drive ±48 dB, wet ±100 %.

**Modulation**: 2 LFOs (7 shapes, 0.01 – 20 Hz or 1/32 – 4 bars synced), input + side-chain envelopes, 8 slots (source → band/all → 15 targets, ±100 %), 32-sample update.

**Global**: oversampling Off / 2x / 4x / 8x (linear phase), HP / LP 12 / 24 dB/oct, bus compressor or **OTT** (3 bands, LR4 crossovers 88 Hz / 2.5 kHz draggable, upward + downward; depth, time, up, down, in, out), noise, clipper, 1 ms look-ahead limiter, mix, output, bypass.

**UI**: mixer page, intensity colours, glow, colour mixing, clash marking, waveform view coloured by band (0.25 – 20 s), animation on the display's vertical blank (60 – 240 Hz) with paint budget, 4 themes (Ember, Sunset, Vintage, Midnight), OpenGL, undo/redo, A/B, 12 factory presets, 1250 parameters (all automatable except chain order).

## Controls

| Action | Result |
|---|---|
| Double-click empty / node | New band / bypass |
| Drag node (Ctrl: Q, Shift: fine) | Frequency + drive |
| Wheel / Ctrl+wheel on node | Q / wet |
| Alt+click node, Delete | Delete band |
| Drag **F** / **T** handle | Filter freq + gain / detector freq + threshold |
| Drag chain tile / right-click | Reorder / reset |
| Drag **OTT** tab | Crossover (double-click: default) |
| Waveform wheel / Ctrl+wheel / double-click | Zoom / speed / freeze |
| Arrows (Shift: fine) | Frequency ±1/12 oct, drive ±1 dB |
| Ctrl+Z / Ctrl+Y | Undo / redo |

## Verification

- Latency reported = measured (44.1 / 48 / 96 kHz); 48 kHz: 48 / 119 / 126 / 129 smp (Off / 2x / 4x / 8x, incl. 1 ms look-ahead).
- Neutral: bit-exact at 1x, null −98 … −116 dB oversampled.
- Aliasing (hard clip, 7.1 kHz, +24 dB): −14.5 dB at 1x, −89 … −93 dB at 2x – 8x.
- Band isolation −0.00 dB; compressor −30 dB 4:1 on −20 dBFS: −27.45 (theory −27.5); limiter peak = ceiling.
- Brickwall: −84 dB ½ oct past the edge; Bell 0.00 dB in band, < −100 dB outside.
- Filter: Peak +12.00 dB, Notch < −250 dB, 24 dB/oct LP −48.2 dB at 2 oct, shelf −8.99 dB (set −9).
- Phase Dist: 2nd harmonic −10 dB at 24 dB, +7 dB at 48 dB, level within 1.4 dB.
- OTT: bands sum flat (0.000 dB, 40 Hz – 15 kHz); −6 dB → −13 dB, −50 dB → +13 dB; silence not lifted.
- Modulation: synced phase 0.2500 after 1/16 at 120 BPM; ±12 dB drive mod exact.
- 20 s random automation of all bands + NaN input: finite and limited.
- CPU (stereo 48 kHz, 24 bands, all modules, 4x): ~44 % of one core (limit 50 %, CI 75 %).
- Host test (1250 params, latency, state) and UI test (drags, presets, themes, corrupt state) pass.

## Limitations

No mid/side or surround; oversampling adds 71 – 81 smp and rolls off > ~20 kHz; 24 full bands at 8x need most of a core; chain order not automatable.

## Licence

GPL-3.0-or-later, © 2026 Roman Gutekunst (GK). No warranty. Uses [JUCE](https://juce.com) 7 (GPLv3), VST3 SDK (Steinberg, GPLv3; VST is a trademark of Steinberg Media Technologies GmbH) and [PFFFT](https://github.com/marton78/pffft) (BSD-style).
