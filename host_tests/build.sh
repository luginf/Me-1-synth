#!/usr/bin/env bash
# Builds m1_render against a converted ROM set (Python reference output).
# rom_compare: g++ -std=c++17 -I ../m1_engine/include -I ../plugin/Source -I <gendir> rom_compare.cpp ../plugin/Source/M1RomLoader.cpp -o rom_compare; ./rom_compare <romdir> <gendir>
#   build.sh <gendir>   gendir = output of tools/m1_make_blob.py (m1_rom_tables.h, m1_pcm.bin)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
GEN="${1:?usage: build.sh <gendir>}"
[ -f "$GEN/m1_rom_tables.h" ] || { echo "no $GEN/m1_rom_tables.h - run tools/m1_make_blob.py first"; exit 1; }
c++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/m1_engine/include" -I"$GEN" \
    "$HERE/m1_render.cpp" \
    "$ROOT/m1_engine/src/m1_engine/m1_engine.cpp" \
    "$ROOT/m1_engine/src/m1_engine/m1_fx.cpp" \
    -o "$HERE/m1_render"
echo "built $HERE/m1_render"
