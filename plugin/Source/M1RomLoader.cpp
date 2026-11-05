// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Michi71 (tables and offsets), Me-1-synth (C++ port)
//
// Port of tools/m1_rom.py, tools/m1_fx_tables.py and tools/m1_make_blob.py.
// Offsets and derivations are documented in doc/ENGINE.md; the Python scripts
// remain the reference (the probe compares both on the owner's ROMs).
#include "M1RomLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace m1rom {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

struct RomError : std::runtime_error { using std::runtime_error::runtime_error; };

constexpr uint32_t kOsSize = 0x20000, kOsBase = 0xE0000, kRev19Ds = 0xF75C0;
constexpr int kMsTable = 0x6978, kMsCount = 100, kMsRec = 14, kSplitRec = 10, kSampleRec = 14;
constexpr int kDwgsFirst = 77, kDwgsStride = 0x3FC;
constexpr uint32_t kPcmWords = 0x200000;
constexpr double kFs = 31250.0;

// ------------------------------------------------------------------ file access
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

Bytes readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw RomError("cannot read " + p.string());
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Dir {
    std::map<std::string, fs::path> byLower;     // lower-case file name -> path
    explicit Dir(const std::string& d) {
        std::error_code ec;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec)) byLower[lower(it->path().filename().string())] = it->path();
    }
    const fs::path* find(const std::string& name) const {
        auto it = byLower.find(lower(name));
        return it == byLower.end() ? nullptr : &it->second;
    }
};

// ------------------------------------------------------------------- OS image
struct Img {
    const Bytes& b;
    explicit Img(const Bytes& x) : b(x) {}
    const uint8_t* lin(uint32_t a, size_t n) const {
        if (a < kOsBase || a - kOsBase + n > kOsSize) throw RomError("address outside the OS image");
        return b.data() + (a - kOsBase);
    }
    uint8_t u8(uint32_t a) const { return *lin(a, 1); }
    uint16_t u16(uint32_t a) const { auto p = lin(a, 2); return (uint16_t)(p[0] | p[1] << 8); }
    uint8_t ds8(uint32_t off) const { return u8(kRev19Ds + off); }
    uint16_t ds16(uint32_t off) const { return u16(kRev19Ds + off); }
    std::vector<uint16_t> ds16s(uint32_t off, int n) const {
        std::vector<uint16_t> v((size_t)n);
        for (int i = 0; i < n; ++i) v[(size_t)i] = ds16(off + 2 * (uint32_t)i);
        return v;
    }
    const uint8_t* ds(uint32_t off, size_t n) const { return lin(kRev19Ds + off, n); }

    static constexpr uint32_t kFx = 0xF2C30;
    static Bytes hex(const char* s) {
        Bytes out;
        for (; s[0] && s[1]; s += 2) { unsigned v; std::sscanf(std::string(s, 2).c_str(), "%2x", &v); out.push_back((uint8_t)v); }
        return out;
    }
    void expect(uint32_t a, const char* hexbytes, const char* what) const {
        const Bytes want = hex(hexbytes);
        if (std::memcmp(lin(a, want.size()), want.data(), want.size()) != 0)
            throw RomError(std::string("not a Korg M1 Rev 19 OS image: ") + what);
    }
    void fx(uint32_t off, const char* hexbytes, const char* what) const { expect(kFx + off, hexbytes, what); }
    int imm16(uint32_t off, const char* opcode, const char* what) const {
        const Bytes op = hex(opcode);
        if (std::memcmp(lin(kFx + off, op.size()), op.data(), op.size()) != 0)
            throw RomError(std::string("not a Korg M1 Rev 19 OS image: ") + what);
        return u16(kFx + off + (uint32_t)op.size());
    }
    int imm8(uint32_t off, const char* opcode, const char* what) const {
        const Bytes op = hex(opcode);
        if (std::memcmp(lin(kFx + off, op.size()), op.data(), op.size()) != 0)
            throw RomError(std::string("not a Korg M1 Rev 19 OS image: ") + what);
        return u8(kFx + off + (uint32_t)op.size());
    }
};

int64_t floordiv(int64_t a, int64_t b) {   // Python //
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// Linear offsets into the 128 KB image (offset 0 = E0000); DS:x is at 0x175C0 + x.
struct Tab { uint32_t off; int n; };
constexpr uint32_t kDsLin = 0x175C0;
constexpr Tab kEgRise{0x10C22, 256}, kEgFall{0x10E22, 256}, kVelCurve{0x116AB, 256}, kMgDelay{0x10B5A, 100},
    kMgIntP{0x109CA, 100}, kMgIntF{0x10A92, 100}, kMgFreq{kDsLin + 0x738E, 100}, kPitchS{kDsLin + 0x0616, 153},
    kPitchF{kDsLin + 0x0748, 128}, kPanReg{kDsLin + 0x0854, 14}, kScale{kDsLin + 0x0848, 12},
    kPan101{kDsLin + 0x8272, 101}, kDelayStart{kDsLin + 0x7230, 100}, kEqK{kDsLin + 0x8966, 5},
    kEqGain{kDsLin + 0x8970, 13};

std::vector<uint16_t> u16s(const Bytes& img, Tab t) {
    std::vector<uint16_t> v((size_t)t.n);
    for (int i = 0; i < t.n; ++i) v[(size_t)i] = (uint16_t)(img[t.off + 2 * (size_t)i] | img[t.off + 2 * (size_t)i + 1] << 8);
    return v;
}
std::vector<int16_t> s16s(const Bytes& img, Tab t) {
    auto u = u16s(img, t);
    return std::vector<int16_t>(u.begin(), u.end());
}

// ------------------------------------------------------------------- ROM set
struct RomSet {
    Bytes pcmBytes, coef, os;
    std::string preloadPath;

    explicit RomSet(const std::string& dir) {
        Dir d(dir);
        loadPcm(d);
        loadCoef(d);
        loadOs(d);
        for (auto& kv : d.byLower) {
            if (kv.first.size() < 4 || kv.first.substr(kv.first.size() - 4) != ".syx") continue;
            const Bytes b = readFile(kv.second);
            if (b.size() > 8 && b[0] == 0xF0 && b[1] == 0x42 && (b[2] & 0xF0) == 0x30 && b[3] == 0x19 && b[4] == 0x50) {
                preloadPath = kv.second.string();
                break;
            }
        }
    }

    void loadPcm(const Dir& d) {
        pcmBytes.assign((size_t)kPcmWords * 2, 0);
        const int lo[4] = {36, 37, 38, 39}, hi[4] = {40, 41, 42, 43};
        for (int k = 0; k < 4; ++k) {
            const auto* lp = d.find("IC" + std::to_string(lo[k]) + ".bin");
            const auto* hp = d.find("IC" + std::to_string(hi[k]) + ".bin");
            if (!lp || !hp) throw RomError("missing IC" + std::to_string(lo[k]) + ".bin / IC" + std::to_string(hi[k]) + ".bin (PCM)");
            const Bytes l = readFile(*lp), h = readFile(*hp);
            if (l.size() != 0x80000 || h.size() != 0x80000)
                throw RomError("IC" + std::to_string(lo[k]) + "/IC" + std::to_string(hi[k]) + ": expected 512 KB each");
            const size_t base = (size_t)k * 0x100000;
            for (size_t i = 0; i < 0x80000; ++i) { pcmBytes[base + 2 * i] = l[i]; pcmBytes[base + 2 * i + 1] = h[i]; }
        }
    }

    static int s8(int v) { return v >= 128 ? v - 256 : v; }

    void loadCoef(const Dir& d) {
        const fs::path* p = d.find("IC30.bin");
        if (!p) p = d.find("IC26.bin");
        if (!p) p = d.find("IC20.bin");
        if (!p) throw RomError("missing IC30.bin (coefficient ROM; IC20/IC26 also do)");
        coef = readFile(*p);
        if (coef.size() != 0x10000) throw RomError(p->filename().string() + ": expected 64 KB");
        // The kernel's tap sum is constant over all phases: cheapest check that this is the right chip.
        for (int ph = 0; ph < 4096; ph += 97) {
            const uint8_t* r = &coef[(size_t)ph * 8];
            const int s = (s8(r[0]) + s8(r[1])) + s8(r[2]) * 2 + (r[3] + r[4]) * 8 + s8(r[5]) * 2 + (s8(r[6]) + s8(r[7]));
            if (s != 1226) throw RomError(p->filename().string() + " is not the M1 coefficient ROM");
        }
    }

    void loadOs(const Dir& d) {
        std::vector<fs::path> cands;
        for (auto& kv : d.byLower) {
            if (kv.first.size() < 4 || kv.first.substr(kv.first.size() - 4) != ".bin") continue;
            if (kv.first == "ic20.bin" || kv.first == "ic26.bin" || kv.first == "ic30.bin") continue;
            std::error_code ec;
            if (fs::file_size(kv.second, ec) == 0x10000) cands.push_back(kv.second);
        }
        for (size_t i = 0; i < cands.size(); ++i)
            for (size_t j = 0; j < cands.size(); ++j) {
                if (i == j) continue;
                const Bytes a = readFile(cands[i]), b = readFile(cands[j]);
                Bytes img(kOsSize);
                for (size_t k = 0; k < 0x10000; ++k) { img[2 * k] = a[k]; img[2 * k + 1] = b[k]; }
                const size_t o = kRev19Ds - kOsBase + kMsTable;
                if (std::memcmp(&img[o + 4], "Piano     ", 10) == 0 && std::memcmp(&img[o + 18], "E.Piano 1 ", 10) == 0) {
                    os = std::move(img);
                    return;
                }
            }
        throw RomError("no original M1 OS found: need the two 64 KB halves of the Rev 19 OS "
                       "(e.g. rev19-IC32-880319.bin + rev19-IC23-880219.bin). The Plus+1 OS is not supported.");
    }
};

std::string rstrip(const uint8_t* p, size_t n) {
    std::string s((const char*)p, n);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r' ||
                          s.back() == '\f' || s.back() == '\v')) s.pop_back();
    return s;
}

std::string cname(std::string s) {   // names go into plain C strings: same substitutions as the generator
    for (auto& c : s) if (c == '"') c = '\'';
    return s;
}
std::string dname(std::string s) {
    for (auto& c : s) { if (c == '\\') c = '/'; else if (c == '"') c = '\''; }
    return s;
}

// ------------------------------------------------------------- effect tables
using Tpl = std::vector<uint64_t>;   // 57 words of 48 bit

constexpr uint32_t kTpl = 0xF4280, kDescTable = 0x836C;
constexpr int kTplWords = 57;

struct Templates {
    const Img& m;
    std::vector<int> offs;
    std::map<int, Tpl> cache;
    explicit Templates(const Img& im) : m(im) {
        for (uint16_t p : m.ds16s(kDescTable, 34)) offs.push_back(m.ds16(p));
        for (int t = 0; t < 33; ++t)
            if (offs[(size_t)t] % 0x15C || offs[(size_t)t] >= 22 * 0x15C)
                throw RomError("not a Korg M1 Rev 19 OS image: effect descriptor");
    }
    const Tpl& operator()(int t) {
        const int o = offs[(size_t)t];
        auto it = cache.find(o);
        if (it == cache.end()) {
            const uint8_t* raw = m.lin(kTpl + (uint32_t)o, 6 * kTplWords);
            Tpl w(kTplWords);
            for (int k = 0; k < kTplWords; ++k) {
                uint64_t v = 0;
                for (int j = 5; j >= 0; --j) v = v << 8 | raw[6 * k + j];
                w[(size_t)k] = v;
            }
            it = cache.emplace(o, std::move(w)).first;
        }
        return it->second;
    }
};

double coefOf(uint64_t w) {   // bits 16-24 magnitude, bit 25 sign, Q9
    const int c = (int)((w >> 16) & 0x3FF);
    return ((c & 0x200) ? -(c & 0x1FF) : (c & 0x1FF)) / 512.0;
}
int addrOf(uint64_t w) { return (int)(w & 0x7FFF); }
int distOf(const Tpl& T, int src, int dst) { return (addrOf(T[(size_t)dst]) - addrOf(T[(size_t)src])) & 0x7FFF; }

int s16v(int64_t v) { v &= 0xFFFF; return (int)(v & 0x8000 ? v - 0x10000 : v); }
int64_t q15mul(int64_t a, int64_t b) {
    const uint64_t p = (uint64_t)((int64_t)s16v(a) * s16v(b)) & 0xFFFFFFFFull;
    return (int64_t)(((p << 1) >> 16) & 0xFFFF);
}

double combFeedback(int W, int signByte, const std::vector<uint16_t>& powTab, int rt, int k1ba1) {
    rt = std::min(rt, 0x62);
    const int64_t q = (W / (rt + 2)) & 0xFFFF;
    const int64_t P = q * k1ba1;
    int64_t ax = (P >> 8) & 0xFFFF;
    const int n = (int)((P >> 24) & 0xFF);
    ax >>= 1;
    const int64_t cx = ax;
    ax >>= 1;
    ax /= 24;
    ax = (ax - 0xAAA) & 0xFFFF;
    int64_t dx = q15mul(ax, cx);
    dx = q15mul((dx + 0x2000) & 0xFFFF, cx);
    dx = q15mul((dx - 0x4000) & 0xFFFF, cx);
    dx = (dx + 0x4000) & 0xFFFF;
    if (n >= (int)powTab.size()) throw RomError("reverb power table index out of range");
    const uint64_t p = (uint64_t)(dx * powTab[(size_t)n]) & 0xFFFFFFFFull;
    dx = (int64_t)(((p << 1) >> 16) & 0xFFFF);
    const int64_t mag = std::min<int64_t>((16 * dx) / 0x1EF, 511);
    return (signByte < 0x80 ? -mag : mag) / 512.0;
}

double combDamp(int Wd, int hd, int k1ef) {
    hd = std::min(hd, 100);
    const int64_t dx = ((int64_t)Wd * k1ef) / 0x200;
    return (double)(((dx * hd) / 100) >> 5) / 512.0;
}

struct RevShared { std::vector<uint16_t> pow, wd, wf; std::vector<int> sign; int k1ef, k1ba1; };

RevShared reverbTables(const Img& m) {
    m.fx(0x098C, "8b97d684", "damp table ref");
    m.fx(0x09D9, "8b871685", "feedback table ref");
    m.fx(0x0A2F, "8b8fc884", "power table ref");
    m.fx(0x0A47, "8a8f5685", "sign table ref");
    RevShared r;
    r.k1ef = m.imm16(0x0990, "b8", "MDE factor 0x1EF");
    r.k1ba1 = m.imm16(0x09EE, "b9", "feedback scale 0x1BA1");
    if (r.k1ef != 0x1EF) throw RomError("not a Korg M1 Rev 19 OS image: MDE factor");
    r.pow = m.ds16s(0x84C8, 7);
    r.wd = m.ds16s(0x84D6, 32);
    r.wf = m.ds16s(0x8516, 32);
    for (int i = 0; i < 32; ++i) r.sign.push_back(m.ds8(0x8556 + (uint32_t)i));
    return r;
}

template <size_t N> void setF(float (&dst)[N], const std::vector<double>& v) {
    for (size_t i = 0; i < N; ++i) dst[i] = i < v.size() ? (float)v[i] : 0.0f;
}
template <size_t N> void setI(int16_t (&dst)[N], const std::vector<int>& v) {
    for (size_t i = 0; i < N; ++i) dst[i] = i < v.size() ? (int16_t)v[i] : (int16_t)0;
}

void fillFb(float (&fb)[4][98], float (&damp)[4][100], const RevShared& rt, int firstComb) {
    for (int k = 0; k < 4; ++k) {
        const int c = firstComb + k;
        for (int r = 0; r < 98; ++r) fb[k][r] = (float)combFeedback(rt.wf[(size_t)c], rt.sign[(size_t)c], rt.pow, r, rt.k1ba1);
        for (int h = 0; h < 100; ++h) damp[k][h] = (float)combDamp(rt.wd[(size_t)c], h, rt.k1ef);
    }
}

// Builds m1::FxTables, including the values the OS does not hold (assumptions
// listed in doc/ENGINE.md; they are returned in `assumed`).
void buildFx(const Bytes& osBytes, m1::FxTables& fx, std::string& assumed) {
    const Img m(osBytes);
    m.fx(0x0A64, "81ff7dc6", "edit buffer compare");
    Templates tpl(m);
    const double G = m.imm16(0x0990, "b8", "MDE factor") / 512.0;

    // 1. Reverbs 0-5
    const RevShared rt = reverbTables(m);
    m.fx(0x0B0F, "81c57685", "pre-delay base table ref");
    m.fx(0x0AA3, "81c5a285", "ER level table ref");
    for (int t = 0; t < 6; ++t) {
        const Tpl& T = tpl(t);
        auto base = m.ds16s(0x8576 + 6 * (uint32_t)t, 3);
        auto ers = m.ds16s(0x85A2 + 6 * (uint32_t)t, 3);
        m1::FxRevType& r = fx.rev[t];
        setF(r.g, {coefOf(T[11]), coefOf(T[16]), coefOf(T[37]), coefOf(T[49])});
        setF(r.comb_out, {coefOf(T[32]), coefOf(T[44]), coefOf(T[34]), coefOf(T[46]), coefOf(T[35]), coefOf(T[47]),
                          coefOf(T[36]), coefOf(T[48])});
        setI(r.ap_d, {distOf(T, 14, 13), distOf(T, 18, 17), distOf(T, 39, 36), distOf(T, 51, 48)});
        setI(r.comb_d, {distOf(T, 23, 19), distOf(T, 26, 22), distOf(T, 27, 25), distOf(T, 30, 28)});
        setI(r.tap_l, {distOf(T, 23, 31), distOf(T, 24, 33), distOf(T, 29, 34), distOf(T, 32, 35)});
        setI(r.tap_r, {distOf(T, 23, 43), distOf(T, 26, 45), distOf(T, 29, 46), distOf(T, 30, 47)});
        r.g_in = 1.0f;
        fillFb(r.fb, r.damp, rt, 4 * t);
        r.base_ap = (int16_t)base[0]; r.base_erl = (int16_t)base[1]; r.base_err = (int16_t)base[2]; r.base_erc = 0;
        setF(r.er_scale, {ers[2] / 512.0, ers[0] / 512.0, ers[1] / 512.0});
    }

    // 2. Delay damping
    const int cE1 = m.imm16(0x05F7, "b9", "damp b range"), c1ef = m.imm16(0x05FC, "b8", "damp b scale"),
              cE1b = m.imm16(0x0601, "b9", "damp b divisor"), c1ef2 = m.imm16(0x0641, "b9", "damp a scale"),
              c12c = m.imm16(0x0646, "b9", "damp a divisor");
    for (int h = 0; h < 100; ++h) {
        fx.dly_a[h] = (float)((double)floordiv((int64_t)h * c1ef2, c12c) / 512.0);
        fx.dly_b[h] = (float)(-(double)floordiv((int64_t)c1ef * (cE1 - h), cE1b) / 512.0);
    }

    // 3. LFO (sine and triangle tables are assumptions, see below)
    m.fx(0x0671, "ac32e43d6300", "LFO speed patcher");
    const int seg1 = m.imm16(0x0674, "3d", "speed segment 1"), mul2 = m.imm16(0x068B, "b9", "speed step 2"),
              off2 = m.imm16(0x0690, "05", "speed base 2"), seg2 = m.imm16(0x0683, "3d", "speed segment 2"),
              mul3 = m.imm16(0x0696, "b9", "speed step 3"), off3 = m.imm16(0x069E, "05", "speed base 3"),
              num = m.imm16(0x06A1, "b9", "speed numerator"), den = m.imm16(0x06A6, "b9", "speed denominator");
    for (int v = 0; v < 217; ++v) {
        int a;
        if (v <= seg1) a = (v + 1) * 3;
        else if (v <= seg2) a = (v - seg1) * mul2 + off2;
        else a = (v - seg2) * mul3 + off3;
        fx.lfo_inc[v] = (int32_t)floordiv((int64_t)a * num, den);
    }

    // 5. Early reflections 6-8
    m.fx(0x0CCC, "8b8d4688", "ER tap table ref");
    const auto tfw = m.ds16s(0x8846, 24);
    const double kMs = (double)m.imm16(0x0CC2, "b9", "ER time scale") / m.imm16(0x0CC7, "b9", "ER time div");
    const double kT = m.imm16(0x0CD2, "b9", "ER tap divisor");
    const double apC[4] = {(double)m.imm16(0x0D05, "b9", "ER AP0 slope"), (double)m.imm16(0x0D26, "b9", "ER AP1 slope"),
                           (double)m.imm16(0x0D47, "b9", "ER AP2 slope"), (double)m.imm16(0x0D70, "b8", "ER AP3 slope")};
    const double kAp = m.imm16(0x0D0A, "b9", "ER AP divisor");
    const double ms = kFs / 1000.0;
    fx.er_T[0] = 0.0f;
    for (int k = 0; k < 24; ++k) fx.er_T[k + 1] = (float)((double)tfw[(size_t)k] * kMs / kT / ms);
    for (int k = 0; k < 4; ++k) fx.er_A[k] = (float)(apC[k] * kMs / kAp);
    for (int t = 6; t <= 8; ++t) {
        const Tpl& T = tpl(t);
        m1::FxErType& e = fx.er[t - 6];
        setF(e.g, {coefOf(T[37]) * G, coefOf(T[41]) * G, coefOf(T[45]) * G, coefOf(T[50]) * G});
        e.tg[0] = (float)coefOf(T[11]);
        for (int k = 1; k <= 24; ++k) e.tg[k] = (float)coefOf(T[(size_t)(12 + k)]);
    }

    // 6. Tremolo shape, exciter blend
    fx.trem_k = (float)((double)m.imm16(0x1160, "b9", "tremolo shape") / m.imm16(0x1165, "b9", "shape div") / 512.0);
    fx.exc_k = (float)(-(double)m.imm16(0x057F, "b9", "exciter scale") / m.imm16(0x0584, "b9", "exciter div") / 512.0);

    // 7. Rotary speaker
    {
        const Tpl& T = tpl(24);
        const int slowK = m.imm16(0x0F4C, "b9", "horn slow slope"), slow0 = m.imm16(0x0F51, "05", "horn slow base"),
                  fastK = m.imm16(0x0F5F, "b9", "horn fast slope"), fast0 = m.imm16(0x0F64, "05", "horn fast base"),
                  div = m.imm16(0x0F68, "b9", "horn div");
        m.expect(0xE05C0, "b0b0e60eb0", "tick timer setup");
        const int tick = m.u8(0xE05C5) | m.u8(0xE05C9) << 8;
        const double tickSamples = tick / 2e6 * kFs;
        const int hornTicks = m.imm8(0x0FB0, "24", "horn step mask") + 1, drumTicks = m.imm8(0x0FAC, "3c", "drum step period");
        m1::FxRotary& r = fx.rot;
        r.c20 = (float)coefOf(T[3]); r.c24 = (float)coefOf(T[11]); r.c28 = (float)coefOf(T[6]);
        r.c30 = (float)coefOf(T[10]); r.c38 = (float)coefOf(T[7]); r.c40 = (float)coefOf(T[4]); r.c44 = (float)coefOf(T[12]);
        r.h_fb_tap = (int16_t)(distOf(T, 4, 6) + 1); r.h_w = 0; r.h_fix = (int16_t)(distOf(T, 4, 11) + 1);
        r.h_base = (int16_t)distOf(T, 4, 18);
        r.d_w = 0; r.d_base = (int16_t)distOf(T, 12, 27);
        setI(r.micL, {distOf(T, 39, 41), distOf(T, 39, 42), distOf(T, 39, 43), distOf(T, 32, 38), distOf(T, 32, 40)});
        setI(r.micR, {distOf(T, 39, 49), distOf(T, 39, 50), distOf(T, 39, 51), distOf(T, 32, 46), distOf(T, 32, 48)});
        r.g120 = (float)coefOf(T[42]); r.g124 = (float)coefOf(T[43]); r.g130 = (float)coefOf(T[39]); r.g134 = (float)coefOf(T[41]);
        r.accel[0] = (int16_t)std::nearbyint(hornTicks * tickSamples);   // Python round: half to even
        r.accel[1] = (int16_t)std::nearbyint(drumTicks * tickSamples);
        for (int s = -10; s <= 10; ++s) {
            r.slow[s + 10] = (int16_t)floordiv((int64_t)slowK * s + slow0, div);
            r.fast[s + 10] = (int16_t)floordiv((int64_t)fastK * s + fast0, div);
        }
        // Values the OS does not hold, assumed (doc/ENGINE.md):
        {
            const double c44 = coefOf(T[12]), c24 = coefOf(T[11]);
            r.c2c = (float)(1.0 - c44 * (1.0 + c24));
        }
        r.depth_k[0] = 0.99f; r.depth_k[1] = 0.99f;
        r.depth_k[2] = (float)(99 * 0.512); r.depth_k[3] = (float)(99 * 0.512);
    }

    // 8. Symphonic ensemble
    fx.sym.inc[0] = (int16_t)m.imm16(0x1290, "b8", "sym LFO1 inc");
    fx.sym.inc[1] = (int16_t)m.imm16(0x1296, "b8", "sym LFO2 inc");
    {
        const int bl[3] = {300 + 154, 300 + 157, 300}, br[3] = {300 + 94, 300 + 217, 300 + 60}, off[3] = {0xA000 >> 9, 0xC000 >> 9, 0xE000 >> 9};
        for (int i = 0; i < 3; ++i) { fx.sym.baseL[i] = (int16_t)bl[i]; fx.sym.baseR[i] = (int16_t)br[i]; fx.sym.off[i] = (int16_t)off[i]; fx.sym.mix2[i] = 0.1f; }
    }

    // 9. Delay/Hall and Delay/Room reverb halves
    m.fx(0x0BD0, "81c59a85", "combo base table ref");
    const auto cb = m.ds16s(0x859A, 4);
    const int cidx[2] = {m.imm16(0x0C3B, "bb", "Delay/Hall comb index"), m.imm16(0x0C66, "bb", "Delay/Room comb index")};
    for (int t = 25; t <= 26; ++t) {
        const Tpl& T = tpl(t);
        m1::FxComboRev& c = fx.crev[t - 25];
        setF(c.g, {coefOf(T[10]), coefOf(T[36]), coefOf(T[15])});
        setF(c.ger, {coefOf(T[40]), coefOf(T[41])});
        setF(c.cout, {coefOf(T[31]), coefOf(T[33]), coefOf(T[34]), coefOf(T[35])});
        setI(c.ap_d, {distOf(T, 13, 12), distOf(T, 38, 37), distOf(T, 17, 16)});
        setI(c.comb_d, {distOf(T, 22, 18), distOf(T, 23, 21), distOf(T, 28, 24), distOf(T, 29, 27)});
        setI(c.comb_tap, {distOf(T, 22, 30), distOf(T, 23, 32), distOf(T, 26, 33), distOf(T, 31, 34)});
        c.g_in = 1.0f;
        fillFb(c.fb, c.damp, rt, cidx[t - 25]);
        c.base_ap = (int16_t)cb[(size_t)(2 * (t - 25))]; c.base_b = (int16_t)cb[(size_t)(2 * (t - 25) + 1)]; c.base_c = 0;
        // Fixed pre-filter: the template's own words apply (assumed decoding, doc/ENGINE.md).
        double w[8];
        for (int k = 0; k < 8; ++k) w[k] = coefOf(T[(size_t)(2 + k)]);
        setF(c.eq, {w[0], w[2] * w[3], w[4], w[6] * w[7], 1.0 + w[1], 1.0 + w[5]});
    }

    // 10. Delay/E.Ref ER half
    {
        const Tpl& T = tpl(27);
        const double g3 = coefOf(T[38]) * G;
        m1::FxComboEr& e = fx.cer;
        setF(e.g, {coefOf(T[30]) * G, coefOf(T[34]) * G, g3, g3});
        e.tg[0] = (float)coefOf(T[4]);
        for (int k = 1; k <= 24; ++k) e.tg[k] = (float)coefOf(T[(size_t)(5 + k)]);
        for (int k = 0; k < 25; ++k) e.T[k] = fx.er_T[k];
        e.A[0] = fx.er_A[0]; e.A[1] = fx.er_A[1]; e.A[2] = fx.er_A[2]; e.A[3] = fx.er_A[2];
        e.g_in = (float)(coefOf(T[2]) * coefOf(T[3]));
    }

    // Shelf-filter tables (straight from the OS)
    {
        auto k = s16s(osBytes, kEqK), g = s16s(osBytes, kEqGain);
        for (int i = 0; i < 5; ++i) fx.eq_k[i] = k[(size_t)i];
        for (int i = 0; i < 13; ++i) fx.eq_gain[i] = g[(size_t)i];
    }

    // LFO waves: not in the OS (the MDE generates them); assumed shapes.
    {
        const double pi = std::acos(-1.0);
        double sine[128], tri[128], w2[128];
        for (int i = 0; i < 128; ++i) {
            const double s = std::sin(pi * (i + 0.5) / 128);
            sine[i] = std::nearbyint(511 * (s * s)) / 512;
            tri[i] = 8.0 * std::min(i, 127 - i) / 512;
            const double s2 = std::sin(2 * pi * (i + 0.5) / 128);
            w2[i] = std::nearbyint(511 * (s2 * s2)) / 512;
        }
        for (float& x : fx.wt) x = 0.0f;
        for (int i = 0; i < 130; ++i) { fx.wt[i] = (float)sine[i % 128]; fx.wt[0x102 + i] = (float)tri[i % 128]; fx.wt2[i] = (float)w2[i % 128]; }
    }
    assumed = "wt, wt2, rotary.c2c, rotary.depth_k, sym.baseL, sym.baseR, sym.off, sym.mix2, crev.eq";
}

// ---------------------------------------------------------------- preload
Bytes unpack7(const uint8_t* b, size_t n) {
    Bytes out;
    for (size_t i = 0; i + 8 <= n; i += 8)
        for (int j = 0; j < 7; ++j) out.push_back((uint8_t)(b[i + 1 + (size_t)j] | (((b[i] >> j) & 1) << 7)));
    return out;
}

}  // namespace

std::shared_ptr<const RomData> loadRomSet(const std::string& dir, std::string& error) {
    try {
        RomSet rom(dir);
        if (rom.preloadPath.empty()) throw RomError("no M1 ALL DATA dump (*.syx) in " + dir);
        const Bytes& img = rom.os;
        const Img m(img);

        // Sanity checks on the located tables: they have to look like what they are.
        {
            auto eqk = s16s(img, kEqK);
            if (eqk[0] != -7790 || eqk[1] != -7407 || eqk[2] != -6690)
                throw RomError("shelf table not where Rev 19 has it - is this the Rev 19 OS?");
            auto p101 = u16s(img, kPan101);
            if (p101[0] != 0 || p101[100] != 511 || p101[50] != 360) throw RomError("balance table not where Rev 19 has it");
            if (u16s(img, kVelCurve)[252] != 15629) throw RomError("velocity curve not where Rev 19 has it");
            if (u16s(img, kPitchS)[128] != 4096) throw RomError("pitch table not where Rev 19 has it");
            if (u16s(img, kMgIntP)[99] != 8191) throw RomError("pitch MG depth table not where Rev 19 has it");
            if ((int8_t)img[kScale.off] != 0 || (int8_t)img[kScale.off + 1] != -38 || (int8_t)img[kScale.off + 2] != 5)
                throw RomError("scale table not where Rev 19 has it");
            if (!(u16s(img, kEgRise)[0] > 0)) throw RomError("EG rise table not where Rev 19 has it");
        }

        auto out = std::make_shared<RomData>();
        out->folder = dir;
        out->pcm.resize(kPcmWords);
        for (uint32_t i = 0; i < kPcmWords; ++i)
            out->pcm[i] = (int16_t)(uint16_t)(rom.pcmBytes[2 * (size_t)i] | rom.pcmBytes[2 * (size_t)i + 1] << 8);

        // Multisounds and splits
        for (int i = 0; i < kMsCount; ++i) {
            const uint8_t* e = m.ds(kMsTable + kMsRec * (uint32_t)i, 4);
            const int ptr = e[0] | e[1] << 8, kind = e[2], nsplit = e[3];
            const std::string name = rstrip(m.ds(kMsTable + kMsRec * (uint32_t)i + 4, 10), 10);
            const bool dwgs = kind == 2;
            const int64_t off = dwgs ? (int64_t)(i - kDwgsFirst) * kDwgsStride : 0;
            m1::Multisound ms{};
            ms.first = (uint16_t)out->splits.size(); ms.count = (uint8_t)nsplit; ms.dwgs = dwgs ? 1 : 0;
            std::snprintf(ms.name, sizeof ms.name, "%s", cname(name).c_str());
            out->msNames.push_back(dname(name));
            for (int k = 0; k < nsplit; ++k) {
                const uint8_t* s = m.ds((uint32_t)(ptr + kSplitRec * k), 9);
                const int rec = s[0] | s[1] << 8;
                const uint8_t* r = m.ds((uint32_t)rec, kSampleRec);
                const int16_t tune128 = (int16_t)(r[0] | r[1] << 8);
                const uint32_t start = r[2] | r[3] << 8 | r[4] << 16, loop = r[6] | r[7] << 8 | r[8] << 16, end = r[10] | r[11] << 8 | r[12] << 16;
                const int flags = r[9], bank = (int)((end >> 20) & 0xF);
                if (bank > 1) throw RomError("multisound " + std::to_string(i) + ": bank is not internal PCM");
                auto a = [&](uint32_t x) { return (uint32_t)((int64_t)(((uint32_t)bank << 20) | (x & 0xFFFFF)) + off); };
                m1::Split sp{};
                sp.start = a(start); sp.loop = a(loop); sp.end = a(end); sp.tune128 = tune128;
                sp.top = s[2]; sp.pan = s[3]; sp.tune = (int8_t)s[4]; sp.root = s[5] & 0x7F;
                sp.flags = (uint8_t)(((flags & 4) ? 1 : 0) | ((s[5] & 0x80) ? 2 : 0));
                sp.level = (int8_t)s[6]; sp.cutoff = (int8_t)s[7]; sp.decay = (int8_t)s[8];
                if (!(sp.start <= sp.end && sp.end < kPcmWords && sp.loop <= sp.end))
                    throw RomError("multisound " + std::to_string(i) + ": addresses out of range");
                out->splits.push_back(sp);
            }
            out->ms.push_back(ms);
        }

        // Drum sounds
        out->drums.push_back(m1::DrumSound{});
        for (int i = 0; i < 44; ++i) {
            const uint8_t* r = m.ds(0x5B9E + kSampleRec * (uint32_t)i, kSampleRec);
            const uint32_t start = r[2] | r[3] << 8 | r[4] << 16, loop = r[6] | r[7] << 8 | r[8] << 16, end = r[10] | r[11] << 8 | r[12] << 16;
            const uint32_t bank = (end >> 20) & 0xF;
            auto a = [&](uint32_t x) { return (bank << 20) | (x & 0xFFFFF); };
            m1::DrumSound d{};
            d.start = a(start); d.loop = a(loop); d.end = a(end); d.flags = (r[9] & 4) ? 1 : 0;
            d.tune128 = (int16_t)(r[0] | r[1] << 8);
            if (bank > 1 || !(d.start <= d.end && d.end < kPcmWords)) throw RomError("drum sound out of range");
            if (d.flags && !(d.loop <= d.end)) throw RomError("drum sound loop past the end");
            out->drums.push_back(d);
        }
        if (std::memcmp(m.ds(0x6F48, 10), "Kick 1    ", 10) != 0) throw RomError("drum-sound names not where Rev 19 has them");
        out->nDrums = 44;
        for (int i = 0; i < 44; ++i) out->drumNames.push_back(dname(rstrip(m.ds(0x6F48 + 11 * (uint32_t)i, 10), 10)));

        // Coefficient ROM
        out->kernel.assign(rom.coef.begin(), rom.coef.begin() + 0x8000);
        auto tabK = [&](uint32_t base) {
            std::vector<uint16_t> v(4096);
            for (uint32_t i = 0; i < 4096; ++i) v[i] = (uint16_t)(rom.coef[base + 0x2000 + i] | rom.coef[base + 0x2000 + 0x1000 + i] << 8);
            return v;
        };
        auto tabT = [&](uint32_t base) {
            std::vector<int16_t> v(4096);
            for (uint32_t i = 0; i < 4096; ++i) v[i] = (int16_t)(uint16_t)(rom.coef[base + i] | rom.coef[base + 0x1000 + i] << 8);
            return v;
        };
        out->vdf1k = tabK(0x8000); out->vdf2k = tabK(0xC000);
        out->vdf1t0 = tabT(0x8000); out->vdf2t0 = tabT(0xC000);

        out->egRise = u16s(img, kEgRise); out->egFall = u16s(img, kEgFall); out->velCurve = u16s(img, kVelCurve);
        out->mgFreq = u16s(img, kMgFreq); out->mgDelay = u16s(img, kMgDelay); out->mgIntP = u16s(img, kMgIntP);
        out->mgIntF = u16s(img, kMgIntF); out->pitchS = u16s(img, kPitchS); out->pitchF = u16s(img, kPitchF);
        out->panReg = u16s(img, kPanReg); out->pan101 = u16s(img, kPan101); out->delayStart = u16s(img, kDelayStart);
        for (int i = 0; i < 12; ++i) out->scale.push_back((int8_t)img[kScale.off + (uint32_t)i]);

        out->fx = std::make_unique<m1::FxTables>();
        std::memset((void*)out->fx.get(), 0, sizeof(m1::FxTables));
        buildFx(img, *out->fx, out->assumedFx);

        // Factory preload (M1 ALL DATA dump)
        {
            const Bytes d = readFile(rom.preloadPath);
            size_t end = 8;
            while (end < d.size() && d[end] != 0xF7) ++end;
            if (end >= d.size()) throw RomError("ALL DATA dump has no end marker");
            const Bytes u = unpack7(&d[8], end - 8);
            if (u.size() < 13261 + 143 * 100) throw RomError("ALL DATA dump too short");
            out->global.assign(u.begin(), u.begin() + 21);
            out->kits.assign(u.begin() + 21, u.begin() + 21 + 210 * 4);
            out->combis.assign(u.begin() + 861, u.begin() + 861 + 124 * 100);
            out->programs.assign(u.begin() + 13261, u.begin() + 13261 + 143 * 100);
        }
        for (auto& s : out->msNames) out->msNamePtrs.push_back(s.c_str());
        for (auto& s : out->drumNames) out->drumNamePtrs.push_back(s.c_str());
        return out;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

}  // namespace m1rom
