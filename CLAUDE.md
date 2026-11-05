# Me-1-synth - notes for Claude

Korg M1 engine extracted from `forks/PicoVintageSynthCollection`
(`instruments/PicoFaceM1`, `tools/m1_extract`), same approach as `d50/` in
`D110/Di-111-synth`. See `README.md` for the layout.

- `m1_engine/` is vendored from upstream: only additive changes, so it can be
  re-synced from the fork.
- Never commit Korg ROMs or anything derived from them (`roms/` is ignored;
  generated `m1_rom_tables.h` and the PCM blob (Python reference output) go in a scratch dir).
- Alan's global rules apply: no em dash, never `find /`.
- `plugin/` is the JUCE shell. Keep `M1_Bridge` unused there (global state);
  per-instance state lives in `M1AudioProcessor`. `plugin/m1_probe.cpp` must keep
  passing, in particular the two-instance bit-identity check.
- The plugin reads the ROMs at runtime (`plugin/Source/M1RomLoader.*`, port of `tools/*.py`); no ROM data is compiled in, so builds can be published, but never publish ROM files or anything the Python tools generate. After touching the loader run `host_tests/rom_compare.cpp` against `tools/m1_make_blob.py` output (see its header).
- GUI checks run in a virtual display (`Xvfb :99`, `DISPLAY=:99`, `import -window root`),
  never on Alan's real screen. Do not find windows with `xdotool search --name`
  (it matches the terminal).
- `plugin/Source/D110Keyboard*`, `D110KeyboardHost.h`, `UiTheme.*` are copies from Di-111-synth: fix bugs there too. `M1AudioProcessor` implements `D110KeyboardHost` and drops the keyboard's 16-channel broadcast (engine is channel-blind).
