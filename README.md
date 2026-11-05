# Me-1-synth

The Korg M1 engine, extracted from Michi71's PicoVintageSynthCollection
(instrument PicoFaceM1, release v1.22.0, 2026-11-05), the same way the D-50
engine was extracted into `Di-111-synth/d50/`.

It is not an emulation of the M1's chips. The envelopes, LFOs and voice
allocation of the V50 firmware are re-implemented, the TG/VDF/VDA chips are
replaced by code that does what the analysis found, and the MDE effects
processor is an algorithmic port of all 33 effect types. `UPSTREAM_README.md`
is the original description (state, ROM provenance, licence) and
`doc/ENGINE.md` the engine write-up (what is firmware-exact, what is assumed).

This directory is the engine only, kept free of any JUCE or Pico UI dependency.

## Layout

- `m1_engine/include/m1_engine/`, `m1_engine/src/m1_engine/` - the synth core
  (`m1_engine`) and the effects (`m1_fx`), vendored unchanged.
- `m1_engine/{include,src}/` - the portable integration layer, also vendored
  unchanged: `M1_Bridge` (engine glue, time source), `m1_memory` (programs,
  combinations, kits, global; the flash mirror is `#if`'d to the RP2350 and
  compiles to RAM-only elsewhere), `M1_SysEx`, `m1_params`, `m1_settings`.
- `tools/` - Python converters: `m1_make_blob.py` (ROMs to PCM blob + table
  header), `m1_rom.py`, `m1_fx_tables.py`.
- `roms/` - gitignored, see `roms/README.md`.
- `doc/ENGINE.md`, `UPSTREAM_README.md` - upstream documentation.

Not taken from upstream (Pico-only: encoder UI, instrument registration, USB and
serial MIDI): `M1_Controller`, `M1_Instrument`, `M1_Midi`, `instrument.cmake`,
and the host tests under `tools/host_tests/m1_engine_test/`.

## Host build

```bash
python3 -I tools/m1_make_blob.py <romdir> <gendir>   # <gendir> outside the repo
host_tests/build.sh <gendir>
host_tests/m1_render <gendir> prog 1 60 100 1.0 2.5 out.wav
host_tests/m1_render <gendir> fxcheck
```

`host_tests/m1_render.cpp` is upstream's renderer (see its header for the other
modes). Checked 2026-11-05 on Linux with the Rev 19 ROM set and `M1preld.syx`:
the engine builds, renders program 1 (Piano 16, peak 0.36) and `fxcheck`
reports 0 failures. Listening against a real M1 has not been done here.

## Plugin (plugin/)

A JUCE plugin (VST3 + Standalone, "Me-1") around the engine.
JUCE is not vendored: `plugin/CMakeLists.txt` points at
`/home/alan/src/juce-checkouts/JUCE-8.0.13` (override with `-DJUCE_DIR=`).

```bash
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release   # converts roms/ at configure time
cmake --build build --target Me-1_Standalone Me-1_VST3 M1Probe -j4
build/M1Probe_artefacts/Release/M1Probe roms/M1preld.syx
```

- `M1AudioProcessor` does not use `M1_Bridge` (its effect RAM, user memory and
  kernel copies are file-scope globals, so two instances would share them).
  Each instance owns its `m1::Engine`, effect RAM and `m1::Memory`; the 31.25 kHz
  output is resampled to the host rate. MIDI mapping is the firmware's
  (`M1_Midi`): omni, CC1/CC2/CC7/CC64/CC65, program change, aftertouch, bend.
  Program Change keeps the current Program/Combi mode.
- The 4 MB PCM is read at runtime from `m1_pcm.bin` (beside the binary, in
  `m1-roms/`, in `~/.config/Me-1/`, or the build tree for dev builds). The
  Standalone and VST3 builds copy it to `m1-roms/` beside the executable (for the
  VST3, inside the bundle, `Contents/x86_64-linux/`); nothing is copied to `~/.vst3`.
- SysEx goes through the vendored `M1_SysEx` (ALL DATA, program and combination
  dumps, mode change, device inquiry). The editor's "Import SysEx..." button
  queues a `.syx` file to the audio thread. Replies are not sent (no MIDI out).
  The user memory is saved in the host state.
- MIDI channels: in Program mode the plugin is omni (any channel plays the program). In
  Combination mode the real channel is passed to the engine, so each timbre answers only on its
  own MIDI Ch (like the hardware: the M1 is multitimbral through Combinations, 8 timbres). The
  factory combinations all use channel 1, so a track on another channel is silent in that mode
  until a timbre's MIDI Ch is changed. The on-screen keyboard reaches every timbre in Combination mode.
- The editor is generated from the engine's parameter tables (`m1_params.h`, the
  same `PageDef`/`ParamDef` the firmware uses) and talks to the engine directly,
  not through the firmware's two-encoder `M1_Controller`. PROG/COMBI, a 100-entry
  selector, volume, voice count, SysEx import and the on-screen keyboard of the JiV-881 and Di-111 projects
  (`D110Keyboard`, vendored with `UiTheme`: octave buttons, right-click menu for MIDI channel/remap,
  velocity, 4-octave mode and tracker-style QWERTY/AZERTY computer-keyboard input, activity LEDs
  for incoming notes; its settings are saved with the host state), plus:
  - an **Edit** tab with every Program or Combination page (follows PROG/COMBI;
    OSC 2 pages only for Double programs; the effect pages follow the effect
    types; 8 timbre pages in a Combination), a text field for the name;
  - **Global** (master tune, key transpose, pedals, scale) and **Drum Kits** tabs;
  - **Compare** (plays and shows the stored version, edits refused meanwhile, Write or a new
    selection leaves it) and **Protect** (the M1's memory protect: Write is refused, also for
    SysEx dumps written through the edit buffer; saved with the host state);
  - graphs above the matching pages: Pitch, VDF and VDA envelopes (segment times from the
    engine's EG rate tables, key-off marked; key and velocity time scaling not applied), the
    VDF frequency response from the engine's coefficient tables (solid at the cutoff,
    dashed at cutoff + EG Int as an approximate peak), and the waveform of the selected
    multisound's zone for C4 on the OSC pages;
  - an **LCD imitation** under the title: 40 x 8 characters of 6 x 8 dots, drawn as lit
    squares with a 5 x 7 bitmap font. Play screens for Program and Combination (name,
    OSC/effects or the 8 timbres), and an edit screen listing the current page's
    parameters in two columns (the one touched last is inverted). Click the screen to
    switch between play and page view. It is **not** the original display: the real
    screens come from the V50 firmware, which is not emulated, and the OS ROM holds
    parameter names but not the layouts. The layouts here are our own;
  - **Write to** a user memory slot and **Revert**. Edits are heard at once (the
    engine reads the edit buffer live) and are only stored by Write. The user
    memory goes into the host state.
  Edits are queued from the UI thread and applied on the audio thread
  (`M1AudioProcessor::requestParam`). Not done: SysEx parameter change (still refused);
  pages with more than 14 parameters (a timbre has 15) hide the last ones on the LCD only.
- The engine is set up in the constructor, not only in `prepareToPlay`, so the
  editor works even when no audio device opens (the Standalone here, without
  ALSA access).

**Not redistributable.** Only the PCM is loaded at runtime. The other ROM-derived
tables (interpolation kernel, VDF coefficients, EG/MG tables, factory programs,
combinations, effect coefficients) come from the OS, IC30 and the preload and are
compiled into the binary through `m1_rom_tables.h`. A build is for the owner of
the ROMs only. A distributable binary needs a C++ port of `tools/m1_rom.py` that
reads those tables from the user's ROMs at startup, as `D5RomLoader` does for the
D-50; not started.

Checked 2026-11-05 on Linux: `M1Probe` (one instance at 48 kHz renders a note, two
interleaved instances give bit-identical output to one alone, all 100 programs and
100 combinations sound and stay finite, edits reach the sound, Write stores them in a slot and leaves the original untouched, global key transpose +12 equals playing an octave higher, Compare shows and plays the stored version and refuses edits, Protect refuses Write, the graph functions give sane data, a saved state restores mode, selection and
volume with an identical render, the factory dump is accepted by SysEx with the same
sound afterwards), the Standalone starts
and stays up. Not checked: the VST3 in a real host, listening against a real M1,
Windows/macOS builds.

## Status

Host engine and plugin work as above. Next: runtime ROM loader (to ship
without ROM-derived data), edit pages, MIDI output for dumps.

## License

GPL-3.0-or-later, as upstream (`LICENSE`). See `CREDITS.md`.
