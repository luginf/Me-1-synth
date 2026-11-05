# roms/

Gitignored: nothing here is ever committed. Korg ROMs are not redistributable.

Put here (same set as upstream, see `UPSTREAM_README.md`, "Building the firmware"):

- `IC36.bin` ... `IC39.bin` - PCM, low bytes (4 x 512 KB)
- `IC40.bin` ... `IC43.bin` - PCM, high bytes (4 x 512 KB)
- `IC30.bin` - coefficient ROM (64 KB; an `IC20.bin` or `IC26.bin` dump has the same content)
- the two halves of the M1 OS (tested with Rev 19; `IC32` even bytes, `IC23` odd
  bytes). Names do not matter, the converter finds the pair by content. The
  Plus+1 OS is refused.
- `*.syx` - an M1 ALL DATA dump, normally the factory preload `M1preld.syx`

`tools/m1_make_blob.py <roms dir> <out dir>` converts them into the PCM blob and
the `m1_rom_tables.h` header that `m1_engine/src/M1_Bridge.cpp` includes.
