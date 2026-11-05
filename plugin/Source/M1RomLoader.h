// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Michi71 (tables and offsets), Me-1-synth (C++ port)
//
// M1RomLoader.h -- reads the user's own Korg M1 ROM files at runtime and builds
// every table the engine needs (C++ port of tools/m1_rom.py, m1_fx_tables.py and
// m1_make_blob.py). Nothing derived from the ROMs is compiled into the binary or
// written to disk: the tables live in memory for the life of the process.
//
// Files expected in the ROM folder (see roms/README.md):
//   IC36..IC39.bin, IC40..IC43.bin   PCM (4 x 512 KB low bytes, 4 x 512 KB high bytes)
//   IC30.bin                         coefficient ROM (64 KB; IC20 / IC26 also accepted)
//   two 64 KB halves of the Rev 19 OS (e.g. rev19-ic32-880319.bin + rev19-ic23-880219.bin)
//   *.syx                            an M1 ALL DATA dump (the factory preload)
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "m1_engine/m1_engine.h"
#include "m1_engine/m1_fx.h"

namespace m1rom {

struct RomData {
    std::vector<int16_t> pcm;                 // 2 M words
    std::vector<m1::Multisound> ms;           // 100
    std::vector<m1::Split> splits;
    std::vector<m1::DrumSound> drums;         // [0] unused, 1..44
    int nDrums = 0;
    std::vector<uint8_t> kernel;              // 4096 x 8
    std::vector<uint16_t> vdf1k, vdf2k, egRise, egFall, velCurve, mgFreq, mgDelay, mgIntP, mgIntF,
                          pitchS, pitchF, panReg, pan101, delayStart;
    std::vector<int16_t> vdf1t0, vdf2t0;
    std::unique_ptr<m1::FxTables> fx;
    std::vector<uint8_t> programs, combis, kits, global;   // factory data (100x143, 100x124, 4x210, 21)
    std::vector<int8_t> scale;                // 12
    std::vector<std::string> msNames, drumNames;
    std::vector<const char*> msNamePtrs, drumNamePtrs;     // views of the two above
    std::string folder;                       // where it was loaded from
    std::string assumedFx;                    // effect values the OS does not hold (doc/ENGINE.md)
};

// Loads the ROM set found in `dir`. Returns null and fills `error` on failure.
std::shared_ptr<const RomData> loadRomSet(const std::string& dir, std::string& error);

}  // namespace m1rom
