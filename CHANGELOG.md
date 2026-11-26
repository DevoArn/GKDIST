# Changelog

## 1.0.0 – 2026-11-26

First public release.

- Up to 24 bands with Bell / Low / High / Full regions, drawn as draggable nodes (frequency × drive) over a live spectrum.
- 10 distortion types: Soft Clip, Hard Clip, Tube, Tape, Transistor, Diode,
  Fuzz, Foldback, Rectify, Bitcrush; drive, bias and auto gain.
- Band regions with 12 / 24 / 48 dB/oct or brickwall slopes.
- Per-band effect chain with a draggable order; click a module to open its
  page. Chips select any of the 24 bands. Modules: filter (LP / HP / BP / Notch /
  Peak / Low and High Shelf / Tilt, 12 – 48 dB/oct, drawn on the display with
  a handle), distortion, Phat (sub-octave weight), noise (white / pink /
  crackle, envelope follow), FX (phaser, flanger, chorus, vibrato, tremolo,
  auto pan), compressor, clip / limit; then wet, gain, pan and mute.
- Per-band dynamics moving gain, drive and wet, detecting in the band's region
  or in an independent bell with a draggable threshold handle and live level,
  internal or external side-chain.
- Mixer page with a strip per band (drive, wet, pan, fader, meter, mute, solo).
- Modulation page: two LFOs (7 shapes, free or tempo-synced), input and
  side-chain envelope followers, 8-slot matrix to any band parameter.
- Visual feedback: band colours saturate with intensity, live glow, colour
  mixing where bands overlap, clash marking between competing bands.
- Waveform view: the output coloured by the bands that change it.
- Colour themes: Ember (default), Sunset, Vintage (warm) and Midnight (cool).
- Global section: oversampling Off / 2x / 4x / 8x, high-/low-pass, bus
  compressor, noise, clipper, 1 ms look-ahead brick-wall limiter, dry/wet
  mix, output gain, latency-compensated bypass.
- Undo/redo, A/B, 12 factory presets and user presets, pre/post analyser,
  resizable GPU-drawn UI.
- Windows installer with uninstaller (VST3 plug-in + standalone app).
