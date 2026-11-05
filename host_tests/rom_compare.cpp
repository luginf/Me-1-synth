// Checks the C++ ROM loader (plugin/Source/M1RomLoader.cpp) against the Python
// generator's output on the same ROM set:  rom_compare <romdir> <gendir>
// <gendir> holds m1_pcm.bin (and m1_rom_tables.h, compiled in) from
// tools/m1_make_blob.py; it stays in a scratch folder, never in the repository.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include "M1RomLoader.h"
#include "m1_rom_tables.h"

static int bad = 0;
template <class V, class T> static void cmpArr(const char* name, const V& a, const T* b, size_t n) {
    const bool ok = a.size() == n && std::memcmp(a.data(), b, n * sizeof(T)) == 0;
    if (!ok) { std::printf("MISMATCH %s (%zu vs %zu)\n", name, (size_t)a.size(), n); ++bad; }
}
int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: rom_compare <romdir> <gendir>\n"); return 2; }
    std::string err;
    auto R = m1rom::loadRomSet(argv[1], err);
    if (!R) { std::printf("load failed: %s\n", err.c_str()); return 2; }
    std::ifstream f(std::string(argv[2]) + "/m1_pcm.bin", std::ios::binary);
    std::vector<char> pcm((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    cmpArr("pcm", R->pcm, (const int16_t*)pcm.data(), pcm.size() / 2);
    cmpArr("ms", R->ms, m1rom::kMultisounds, 100);
    cmpArr("splits", R->splits, m1rom::kSplits, sizeof m1rom::kSplits / sizeof(m1::Split));
    cmpArr("drums", R->drums, m1rom::kDrums, sizeof m1rom::kDrums / sizeof(m1::DrumSound));
    if (R->nDrums != m1rom::kDrumCount) { std::printf("MISMATCH nDrums\n"); ++bad; }
    cmpArr("kernel", R->kernel, m1rom::kKernel, sizeof m1rom::kKernel);
    cmpArr("vdf1k", R->vdf1k, m1rom::kVdf1k, 4096); cmpArr("vdf2k", R->vdf2k, m1rom::kVdf2k, 4096);
    cmpArr("vdf1t0", R->vdf1t0, m1rom::kVdf1t0, 4096); cmpArr("vdf2t0", R->vdf2t0, m1rom::kVdf2t0, 4096);
    cmpArr("egRise", R->egRise, m1rom::kEgRise, 256); cmpArr("egFall", R->egFall, m1rom::kEgFall, 256);
    cmpArr("velCurve", R->velCurve, m1rom::kVelCurve, 256);
    cmpArr("mgFreq", R->mgFreq, m1rom::kMgFreq, 100); cmpArr("mgDelay", R->mgDelay, m1rom::kMgDelay, 100);
    cmpArr("mgIntP", R->mgIntP, m1rom::kMgIntP, 100); cmpArr("mgIntF", R->mgIntF, m1rom::kMgIntF, 100);
    cmpArr("pitchS", R->pitchS, m1rom::kPitchS, 153); cmpArr("pitchF", R->pitchF, m1rom::kPitchF, 128);
    cmpArr("panReg", R->panReg, m1rom::kPanReg, 14); cmpArr("pan101", R->pan101, m1rom::kPan101, 101);
    cmpArr("delayStart", R->delayStart, m1rom::kDelayStart, 100);
    cmpArr("programs", R->programs, m1rom::kPrograms, sizeof m1rom::kPrograms);
    cmpArr("combis", R->combis, m1rom::kCombis, sizeof m1rom::kCombis);
    cmpArr("kits", R->kits, m1rom::kKits, sizeof m1rom::kKits);
    cmpArr("global", R->global, m1rom::kGlobal, sizeof m1rom::kGlobal);
    cmpArr("scale", R->scale, m1rom::kScale, 12);
    for (int i = 0; i < 100; ++i)
        if (R->msNames[(size_t)i] != m1rom::kMultisoundNames[i]) { std::printf("MISMATCH ms name %d: '%s' vs '%s'\n", i, R->msNames[(size_t)i].c_str(), m1rom::kMultisoundNames[i]); ++bad; }
    for (int i = 0; i < 44; ++i)
        if (R->drumNames[(size_t)i] != m1rom::kDrumNames[i]) { std::printf("MISMATCH drum name %d\n", i); ++bad; }
    if (R->assumedFx != m1rom::kFxAssumed) { std::printf("MISMATCH assumed list: '%s' vs '%s'\n", R->assumedFx.c_str(), m1rom::kFxAssumed); ++bad; }
    // Effect tables: field by field through the aggregate, then the whole struct.
    const m1::FxTables &a = *R->fx, &b = m1rom::kFx;
    auto fld = [&](const char* n, const void* x, const void* y, size_t len) {
        if (std::memcmp(x, y, len) != 0) { std::printf("MISMATCH fx.%s\n", n); ++bad; }
    };
    for (int t = 0; t < 6; ++t) {
        char n[32];
        std::snprintf(n, sizeof n, "rev[%d]", t);
        fld(n, &a.rev[t], &b.rev[t], sizeof(m1::FxRevType));
    }
    fld("dly_a", a.dly_a, b.dly_a, sizeof a.dly_a); fld("dly_b", a.dly_b, b.dly_b, sizeof a.dly_b);
    fld("wt", a.wt, b.wt, sizeof a.wt); fld("wt2", a.wt2, b.wt2, sizeof a.wt2);
    fld("lfo_inc", a.lfo_inc, b.lfo_inc, sizeof a.lfo_inc);
    fld("er", a.er, b.er, sizeof a.er); fld("er_T", a.er_T, b.er_T, sizeof a.er_T); fld("er_A", a.er_A, b.er_A, sizeof a.er_A);
    fld("trem_k", &a.trem_k, &b.trem_k, 4); fld("exc_k", &a.exc_k, &b.exc_k, 4);
    fld("rot", &a.rot, &b.rot, sizeof a.rot); fld("sym", &a.sym, &b.sym, sizeof a.sym);
    fld("crev", a.crev, b.crev, sizeof a.crev); fld("cer", &a.cer, &b.cer, sizeof a.cer);
    fld("eq_k", a.eq_k, b.eq_k, sizeof a.eq_k); fld("eq_gain", a.eq_gain, b.eq_gain, sizeof a.eq_gain);
    std::printf(bad ? "rom_compare: %d MISMATCHES\n" : "rom_compare: all tables identical (%d)\n", bad);
    return bad ? 1 : 0;
}
