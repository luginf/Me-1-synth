#include "M1AudioProcessor.h"

#include <cmath>
#include <complex>

#include "M1Editor.h"
#include "m1_rom_tables.h"   // generated at configure time from the user's ROMs

namespace {
// The engine's output stage on the Pico (M1_Bridge) is gain * midiGain * 2.0
// with a soft clip above 0.9. Here the same 2.0 makeup, soft clip kept.
constexpr float kMakeup = 2.0f;

inline float softClip(float x) {
    const float a = 0.9f, range = 1.0f - a;
    if (x > a)  return  a + range * (1.0f - 1.0f / (1.0f + (x - a) / range));
    if (x < -a) return -a - range * (1.0f - 1.0f / (1.0f + (-x - a) / range));
    return x;
}
}  // namespace

// M1_SysEx's output hooks. The plugin has no MIDI output yet (producesMidi is
// false), so a dump or inquiry request is parsed but its reply is not sent.
size_t m1_sysex_room() { return 0; }
void m1_sysex_write(const uint8_t*, size_t) {}

M1AudioProcessor::M1AudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    sysex.reset(new M1_SysEx(*this));   // not make_unique: the host base is private
    // Static factory data only: these pointers are process-wide, so they must not
    // point into an instance (the Program names of timbres are resolved by
    // formatParam() from the instance's own memory instead).
    m1::setNameSources(m1rom::kMultisoundNames, m1rom::kDrumNames, m1rom::kPrograms);
    initEngine();
}

M1AudioProcessor::~M1AudioProcessor() = default;

// m1_pcm.bin is the 4 MiB interleaved PCM written by tools/m1_make_blob.py.
// Search order: beside the binary, in m1-roms/ beside it, in the per-user data
// folder, then the build tree it was generated in (dev builds).
bool M1AudioProcessor::loadPcm() {
    const auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    const juce::Array<juce::File> candidates{
        exeDir.getChildFile("m1_pcm.bin"),
        exeDir.getChildFile("m1-roms").getChildFile("m1_pcm.bin"),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Me-1").getChildFile("m1_pcm.bin"),
#ifdef M1_PCM_DEV_PATH
        juce::File(M1_PCM_DEV_PATH),
#endif
    };
    for (const auto& f : candidates) {
        if (!f.existsAsFile()) continue;
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) continue;
        if (mb.getSize() != 4u * 1024u * 1024u) {
            status = "Wrong size for " + f.getFullPathName() + " (need 4 MiB)";
            continue;
        }
        pcm.resize(mb.getSize() / 2);
        std::memcpy(pcm.data(), mb.getData(), mb.getSize());   // little-endian host
        status = "M1 ready (" + f.getFullPathName() + ")";
        return true;
    }
    if (status.isEmpty())
        status = "m1_pcm.bin not found next to the app, in m1-roms/ or in the user data folder";
    return false;
}

void M1AudioProcessor::prepareToPlay(double sampleRate, int) {
    hostSampleRate = sampleRate;
    resamplerL.reset();
    resamplerR.reset();
    pendingL.clear();
    pendingR.clear();
    injectedMidi.reset(sampleRate);
    initEngine();
}

// Engine setup does not depend on the sample rate, so it also runs from the
// constructor: the editor must have something to show even when the host (or
// the Standalone, without an audio device) never calls prepareToPlay.
void M1AudioProcessor::initEngine() {
    if (ready) return;
    if (!loadPcm()) return;

    mem.init(m1rom::kGlobal, m1rom::kKits, m1rom::kCombis, m1rom::kPrograms);
    if (stateMemory.size() == sizeof(m1::MemoryImage))
        std::memcpy(&mem.image(), stateMemory.data(), stateMemory.size());
    stateMemoryPending = false;
    m1::MemoryImage& img = mem.image();
    m1::RomTables rom{};
    rom.pcm = pcm.data();
    rom.pcmWords = (uint32_t)pcm.size();
    rom.ms = m1rom::kMultisounds;
    rom.splits = m1rom::kSplits;
    rom.drums = m1rom::kDrums;
    rom.nDrums = m1rom::kDrumCount;
    rom.kernel = m1rom::kKernel;
    rom.vdf1k = m1rom::kVdf1k;
    rom.vdf2k = m1rom::kVdf2k;
    rom.vdf1t0 = m1rom::kVdf1t0;
    rom.vdf2t0 = m1rom::kVdf2t0;
    rom.egRise = m1rom::kEgRise;
    rom.egFall = m1rom::kEgFall;
    rom.velCurve = m1rom::kVelCurve;
    rom.mgFreq = m1rom::kMgFreq;
    rom.mgDelay = m1rom::kMgDelay;
    rom.mgIntP = m1rom::kMgIntP;
    rom.mgIntF = m1rom::kMgIntF;
    rom.pitchS = m1rom::kPitchS;
    rom.pitchF = m1rom::kPitchF;
    rom.panReg = m1rom::kPanReg;
    rom.pan101 = m1rom::kPan101;
    rom.delayStart = m1rom::kDelayStart;
    rom.fx = &m1rom::kFx;
    rom.programs = img.programs;
    rom.combis = img.combis;
    rom.kits = img.kits;
    rom.global = img.global;
    rom.scale = m1rom::kScale;
    engine = std::make_unique<m1::Engine>();
    if (!engine->init(rom, fxMem.data())) {
        status = "Engine init failed";
        engine.reset();
        return;
    }
    select(combiMode.load(), index.load());
    ready = true;
}

void M1AudioProcessor::select(bool combi, int i) {
    i = juce::jlimit(0, 99, i);
    combiMode = combi;
    index = i;
    if (!engine) return;
    {
        // The engine plays from the edit buffer and reads it live, so edits are heard at once.
        std::lock_guard<std::mutex> lock(editMutex);
        if (combi) std::memcpy(combiEditBuf, mem.combi(i), sizeof combiEditBuf);
        else std::memcpy(progEdit, mem.program(i), sizeof progEdit);
    }
    if (combi) engine->playCombi(combiEditBuf); else engine->playProgram(progEdit);
    applyGlobal();
    editedFlag = false;
    comparing = false;
    ++editRev;
}

void M1AudioProcessor::applyGlobal() {
    if (engine) engine->setMasterTune((int8_t)mem.image().global[0]);   // the M1's global Master Tune
}

uint8_t* M1AudioProcessor::blockFor(EditTarget t, int kit, int inst) {
    m1::MemoryImage& img = mem.image();
    switch (t) {
        case EditTarget::Program: return progEdit;
        case EditTarget::Combi:   return combiEditBuf;
        case EditTarget::Global:  return img.global;
        case EditTarget::Kit:     return img.kits + 210 * juce::jlimit(0, 3, kit) + 7 * juce::jlimit(0, 29, inst);
    }
    return progEdit;
}

void M1AudioProcessor::requestParam(EditTarget t, const m1::ParamDef& p, int value, int kit, int inst) {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back({0, t, p, value, kit, inst, 0, false});
}
void M1AudioProcessor::requestWrite(bool combi, int dest) {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back({1, EditTarget::Program, {}, 0, 0, 0, dest, combi});
}
void M1AudioProcessor::requestCompare(bool on) {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back({3, EditTarget::Program, {}, on ? 1 : 0, 0, 0, 0, false});
}
void M1AudioProcessor::requestProtect(bool on) {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back({4, EditTarget::Program, {}, on ? 1 : 0, 0, 0, 0, false});
}
void M1AudioProcessor::requestRevert() {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back({2, EditTarget::Program, {}, 0, 0, 0, 0, false});
}

void M1AudioProcessor::getEditBlock(EditTarget t, int kit, int inst, uint8_t* out, size_t cap) const {
    std::lock_guard<std::mutex> lock(editMutex);
    auto* self = const_cast<M1AudioProcessor*>(this);
    const size_t n = t == EditTarget::Program ? 143 : t == EditTarget::Combi ? 124 : t == EditTarget::Global ? 21 : 7;
    const uint8_t* src = self->blockFor(t, kit, inst);
    if (comparing.load() && t == EditTarget::Program) src = self->mem.program(index.load());
    if (comparing.load() && t == EditTarget::Combi) src = self->mem.combi(index.load());
    std::memcpy(out, src, juce::jmin(n, cap));
}

juce::String M1AudioProcessor::formatParam(const m1::ParamDef& p, const uint8_t* block) const {
    if (p.kind == m1::PK::Program) {
        const int v = juce::jlimit(0, 99, m1::paramGet(p, block));
        return juce::String(v).paddedLeft('0', 2) + " " + nameAt(false, v);
    }
    char buf[40];
    m1::paramFormat(p, block, buf, sizeof buf, nullptr);
    return juce::String(buf).trim();
}

// What the firmware's M1_Controller::edited() does after a parameter change.
void M1AudioProcessor::applyEdit(const EditCmd& c) {
    if (!engine) return;
    if (c.kind == 1) {                       // write
        if (mem.protect) return;
        {
            std::lock_guard<std::mutex> lock(editMutex);
            if (c.combi) writeCombi(c.dest); else writeProgram(c.dest);
        }
        index = juce::jlimit(0, 99, c.dest);
        editedFlag = false;
        if (comparing.load()) {          // as on the M1, Write leaves Compare
            comparing = false;
            if (c.combi) engine->playCombi(combiEditBuf); else engine->playProgram(progEdit);
        }
        ++editRev;
        return;
    }
    if (c.kind == 2) { select(combiMode.load(), index.load()); return; }
    if (c.kind == 3) {                       // compare on/off
        const bool on = c.value != 0;
        if (on == comparing.load()) return;
        comparing = on;
        if (combiMode.load()) engine->playCombi(on ? mem.combi(index.load()) : combiEditBuf);
        else engine->playProgram(on ? mem.program(index.load()) : progEdit);
        ++editRev;
        return;
    }
    if (c.kind == 4) { protectFlag = c.value != 0; mem.protect = c.value != 0; ++editRev; return; }
    if (comparing.load() && (c.target == EditTarget::Program || c.target == EditTarget::Combi)) return;   // compare shows the stored version

    {
        std::lock_guard<std::mutex> lock(editMutex);
        uint8_t* blk = blockFor(c.target, c.kit, c.inst);
        m1::paramSet(c.p, blk, c.value);
    }
    const int off = c.p.offset;
    switch (c.target) {
        case EditTarget::Program:
            editedFlag = true;
            if (!combiMode.load() && off >= m1::kProgFx && off < m1::kProgFx + 25)
                engine->refreshEffects(progEdit + m1::kProgFx);
            break;
        case EditTarget::Combi:
            editedFlag = true;
            if (off >= m1::kCombiFx && off < m1::kCombiFx + 25) {
                if (combiMode.load()) engine->refreshEffects(combiEditBuf + m1::kCombiFx);
            } else if (off >= 10 && combiMode.load()) {
                engine->playCombi(combiEditBuf);   // timbre settings are taken at selection
            }
            break;
        case EditTarget::Global:
            mem.touch(0, 21);
            applyGlobal();
            break;
        case EditTarget::Kit:
            mem.touch(21 + 210 * (size_t)juce::jlimit(0, 3, c.kit) + 7 * (size_t)juce::jlimit(0, 29, c.inst), 7);
            engine->rebuildKits();
            break;
    }
    ++editRev;
}

double M1AudioProcessor::egSegmentSeconds(int timeParam, bool rising) {
    int t = juce::jlimit(0, 99, timeParam) * 165;
    if (t > 0x3FFF) t = 0x3FFF;
    const int rate = (rising ? m1rom::kEgRise : m1rom::kEgFall)[t >> 6];
    return 65536.0 / (double)(rate < 1 ? 1 : rate) * (double)m1::kUpdate / (double)m1::kSampleRate;
}

// The engine's VDF (m1_engine.cpp, vdf()): two chips, each a plain one-pole stage (k) and a
// bilinear stage (pole a, gain (1 - a) / 2), run at twice the output rate.
std::vector<float> M1AudioProcessor::vdfResponseDb(int cutoffParam, const std::vector<float>& freqsHz) {
    int c = (juce::jlimit(0, 99, cutoffParam) * 0x52BF) >> 8;
    c = juce::jlimit(0, 0x1FFF, c);
    const int i2 = c >> 1, i1 = juce::jmin(c, 0x1FD2) >> 1;
    const double k1 = m1rom::kVdf1k[i1] / 8192.0, k2 = m1rom::kVdf2k[i2] / 8192.0;
    const double a1 = -m1rom::kVdf1t0[i1] / 8192.0, a2 = -m1rom::kVdf2t0[i2] / 8192.0;
    const double g1 = (1.0 - a1) * 0.5, g2 = (1.0 - a2) * 0.5;
    const double fs = 2.0 * m1::kSampleRate;
    std::vector<float> out;
    for (float f : freqsHz) {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const std::complex<double> z1 = std::polar(1.0, -w);
        auto onePole = [&](double k) { return k / (1.0 - (1.0 - k) * z1); };
        auto bilinear = [&](double a, double g) { return g * (1.0 + z1) / (1.0 - a * z1); };
        const auto h = onePole(k1) * bilinear(a1, g1) * onePole(k2) * bilinear(a2, g2);
        out.push_back((float)(20.0 * std::log10(std::max(1.0e-6, std::abs(h)))));
    }
    return out;
}

int M1AudioProcessor::multisoundWave(int ms, int key, int columns, std::vector<float>& mins, std::vector<float>& maxs, int& zones) const {
    zones = 0;
    if (!ready || ms < 0 || ms > 99 || columns < 1) return 0;
    const m1::Multisound& m = m1rom::kMultisounds[ms];
    zones = m.count;
    int zone = 0;
    for (int k = 0; k < m.count; ++k) {
        zone = k;
        if (key <= m1rom::kSplits[m.first + k].top) break;
    }
    const m1::Split& sp = m1rom::kSplits[m.first + zone];
    if (sp.end <= sp.start || sp.end > pcm.size()) return 0;
    const uint32_t n = sp.end - sp.start;
    mins.assign((size_t)columns, 0.0f);
    maxs.assign((size_t)columns, 0.0f);
    for (int x = 0; x < columns; ++x) {
        const uint32_t a = sp.start + (uint32_t)((uint64_t)n * (uint64_t)x / (uint64_t)columns);
        uint32_t b = sp.start + (uint32_t)((uint64_t)n * (uint64_t)(x + 1) / (uint64_t)columns);
        if (b <= a) b = a + 1;
        int lo = 32767, hi = -32768;
        const uint32_t step = juce::jmax<uint32_t>(1, (b - a) / 64);   // enough samples per column
        for (uint32_t i = a; i < b && i < sp.end; i += step) { lo = std::min<int>(lo, pcm[i]); hi = std::max<int>(hi, pcm[i]); }
        mins[(size_t)x] = (float)lo / 32768.0f;
        maxs[(size_t)x] = (float)hi / 32768.0f;
    }
    return zone + 1;
}

juce::String M1AudioProcessor::nameAt(bool combi, int i) const {
    if (i < 0 || i > 99) return {};
    const uint8_t* p = combi ? m1rom::kCombis + 124 * i : m1rom::kPrograms + 143 * i;
    if (ready) p = combi ? mem.image().combis + 124 * i : mem.image().programs + 143 * i;
    char buf[11];
    std::memcpy(buf, p, 10);
    buf[10] = 0;
    for (auto& c : buf) if (c < 32 || c > 126) c = ' ';
    return juce::String(buf).trimEnd();
}

void M1AudioProcessor::requestSelect(bool combi, int i) { pendingSelect = juce::jlimit(0, 99, i) | (combi ? 0x100 : 0); }

// D110Keyboard broadcasts each key on all 16 channels when "MIDI Remap" is off. The engine
// ignores the channel in Program mode, so 15 of the 16 would be duplicates that eat voices.
void M1AudioProcessor::injectTestNote(int channel, int note, float velocity, bool on) {
    // Program mode ignores the channel, so the 16-channel broadcast would play the key 16 times.
    // In Combination mode each timbre listens on one channel, so the broadcast reaches each timbre once.
    if (!kbRemap && channel != 1 && !combiMode.load()) return;
    auto m = on ? juce::MidiMessage::noteOn(channel, note, velocity) : juce::MidiMessage::noteOff(channel, note, velocity);
    m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);   // what MidiMessageCollector expects
    injectedMidi.addMessageToQueue(m);
}

juce::String M1AudioProcessor::importSyx(const juce::File& file) {
    juce::MemoryBlock mb;
    if (!file.loadFileAsData(mb) || mb.getSize() < 8) return "Could not read " + file.getFileName();
    {
        std::lock_guard<std::mutex> lock(syxMutex);
        const auto* d = static_cast<const uint8_t*>(mb.getData());
        pendingSyx.insert(pendingSyx.end(), d, d + mb.getSize());
    }
    return "Queued " + file.getFileName() + " (" + juce::String((int)mb.getSize()) + " bytes)";
}

// ---------------------------------------------------------------------------
// SysEx host callbacks (audio thread, from sysex->feed)
void M1AudioProcessor::setM1Mode(int m) {
    // 0/1 combination, 2/3 program; 4 global is accepted and ignored here.
    if (m == 0 || m == 1) select(true, index.load());
    else if (m == 2 || m == 3) select(false, index.load());
}
void M1AudioProcessor::programEditLoaded() {
    if (!combiMode.load() && engine) engine->playProgram(progEdit);
}
void M1AudioProcessor::combiEditLoaded() {
    if (combiMode.load() && engine) engine->playCombi(combiEditBuf);
}
bool M1AudioProcessor::writeProgram(int n) {
    if (n < 0 || n > 99 || mem.protect) return false;
    std::memcpy(mem.program(n), progEdit, sizeof progEdit);
    mem.touch(offsetof(m1::MemoryImage, programs) + 143u * (size_t)n, 143);
    return true;
}
bool M1AudioProcessor::writeCombi(int n) {
    if (n < 0 || n > 99 || mem.protect) return false;
    std::memcpy(mem.combi(n), combiEditBuf, sizeof combiEditBuf);
    mem.touch(offsetof(m1::MemoryImage, combis) + 124u * (size_t)n, 124);
    return true;
}
void M1AudioProcessor::memoryLoaded() {
    if (!engine) return;
    engine->rebuildKits();
    select(combiMode.load(), index.load());
}

// ---------------------------------------------------------------------------
void M1AudioProcessor::applyPending() {
    if (stateMemoryPending) {
        std::unique_lock<std::mutex> lock(syxMutex, std::try_to_lock);
        if (lock.owns_lock() && stateMemoryPending) {
            std::memcpy(&mem.image(), stateMemory.data(), stateMemory.size());
            stateMemoryPending = false;
            engine->rebuildKits();
            select(combiMode.load(), index.load());
        }
    }
    const int p = pendingSelect.exchange(-1);
    if (p >= 0) select((p & 0x100) != 0, p & 0xFF);
    std::vector<EditCmd> todo;
    {
        std::unique_lock<std::mutex> lock(cmdMutex, std::try_to_lock);
        if (lock.owns_lock()) todo.swap(cmds);   // swapping an empty vector allocates nothing
    }
    for (const auto& c : todo) applyEdit(c);
}

// Same mapping as the firmware's M1_Midi: the engine is driven on channel 0,
// the M1's global channel being omni here. CC1/CC2 joystick, CC7 volume, CC64
// damper, CC65 rotary speed.
void M1AudioProcessor::handleMidi(const juce::MidiMessage& m) {
    if (!engine) return;
    if (m.isSysEx()) {
        sysex->feed(m.getSysExData() - 1, (size_t)m.getSysExDataSize() + 2);   // F0 .. F7
        return;
    }
    const auto b = m.getRawData();
    // activity flags for the on-screen keyboard's LEDs: the note as played, before the global transpose
    if (m.isNoteOn()) noteActive[(size_t)m.getNoteNumber()].store(true, std::memory_order_relaxed);
    else if (m.isNoteOff()) noteActive[(size_t)m.getNoteNumber()].store(false, std::memory_order_relaxed);
    else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& f : noteActive) f.store(false, std::memory_order_relaxed);
    // Combination mode is the M1's multitimbral mode: each timbre listens on its own MIDI channel
    // (its "MIDI Ch" parameter), so the real channel goes to the engine. Program mode is
    // monotimbral and takes any channel (omni), as before.
    const uint8_t ch = (combiMode.load() && m.getChannel() >= 1) ? (uint8_t)(m.getChannel() - 1) : (uint8_t)0;
    const int transpose = (int8_t)mem.image().global[1];   // the M1's global key transpose
    const int tn = m.isNoteOnOrOff() ? m.getNoteNumber() + transpose : 0;
    if (m.isNoteOnOrOff() && (tn < 0 || tn > 127)) return;
    if (m.isNoteOn())            engine->noteOn(ch, (uint8_t)tn, m.getVelocity());
    else if (m.isNoteOff())      engine->noteOff(ch, (uint8_t)tn);
    else if (m.isPitchWheel())   engine->pitchBend(ch, (int16_t)(m.getPitchWheelValue() - 8192));
    else if (m.isChannelPressure()) engine->aftertouch(ch, (uint8_t)m.getChannelPressureValue());
    else if (m.isProgramChange()) {
        const int pc = m.getProgramChangeNumber();
        if (pc <= 99) select(combiMode.load(), pc);
    } else if (m.isController()) {
        const int v = m.getControllerValue();
        switch (m.getControllerNumber()) {
            case 1:   engine->modWheel(ch, (uint8_t)v); break;
            case 2:   engine->modWheelNeg(ch, (uint8_t)v); break;
            case 7:   { const float x = (float)v / 127.0f; midiGain = x * x; } break;
            case 64:  engine->damper(ch, v >= 64); break;
            case 65:  engine->setRotaryFast(v >= 64); break;
            case 120: engine->allSoundOff(); break;
            case 121:
                engine->modWheel(ch, 0); engine->modWheelNeg(ch, 0);
                engine->aftertouch(ch, 0); engine->pitchBend(ch, 0);
                engine->damper(ch, false); midiGain = 1.0f;
                break;
            case 123: case 124: case 125: case 126: case 127: engine->allNotesOff(); break;
            default: break;
        }
    }
    (void)b;
}

void M1AudioProcessor::renderInternal(int n, float* outL, float* outR) {
    if (!ready || !engine) {
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    const double ratio = (double)m1::kSampleRate / hostSampleRate;
    const int needed = (int)std::ceil(n * ratio) + 4;
    const int have = (int)pendingL.size();
    if (have < needed) {
        const int toRender = needed - have;
        scratchL.resize((size_t)toRender);
        scratchR.resize((size_t)toRender);
        engine->render(scratchL.data(), scratchR.data(), toRender);
        const float vol = (float)volumePercent.load() * 0.01f;
        const float g = vol * vol * midiGain * kMakeup;
        for (int i = 0; i < toRender; ++i) {
            pendingL.push_back(softClip(scratchL[(size_t)i] * g));
            pendingR.push_back(softClip(scratchR[(size_t)i] * g));
        }
    }
    const int usedL = resamplerL.process(ratio, pendingL.data(), outL, n);
    const int usedR = resamplerR.process(ratio, pendingR.data(), outR, n);
    const int used = juce::jmax(usedL, usedR);
    pendingL.erase(pendingL.begin(), pendingL.begin() + used);
    pendingR.erase(pendingR.begin(), pendingR.begin() + used);
    activeVoicesShown = engine->activeVoices();
}

void M1AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    juce::MidiBuffer fromKeyboard;
    injectedMidi.removeNextBlockOfMessages(fromKeyboard, n);
    for (const auto md : fromKeyboard) midi.addEvent(md.getMessage(), md.samplePosition);

    if (ready) {
        applyPending();
        {
            std::unique_lock<std::mutex> lock(syxMutex, std::try_to_lock);
            if (lock.owns_lock() && !pendingSyx.empty()) {
                sysex->feed(pendingSyx.data(), pendingSyx.size());
                pendingSyx.clear();
            }
        }
        for (const auto md : midi) handleMidi(md.getMessage());
    }

    if (buffer.getNumChannels() >= 2) {
        renderInternal(n, buffer.getWritePointer(0), buffer.getWritePointer(1));
    } else if (buffer.getNumChannels() == 1) {
        std::vector<float> r((size_t)n);
        renderInternal(n, buffer.getWritePointer(0), r.data());
    }
}

// State: mode, selection, volume, and the full user memory image so that a
// loaded bank survives a project reload.
void M1AudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(0x4D314D31);   // 'M1M1'
    out.writeInt(1);
    out.writeInt(combiMode.load() ? 1 : 0);
    out.writeInt(index.load());
    out.writeInt(volumePercent.load());
    out.writeInt(ready ? (int)sizeof(m1::MemoryImage) : 0);
    if (ready) out.write(&mem.image(), sizeof(m1::MemoryImage));
    out.writeInt(protectFlag.load() ? 1 : 0);
    out.writeInt(kbChannel); out.writeInt(kbPcInput ? 1 : 0); out.writeInt(kbPcLayout);
    out.writeInt(kbOctaves); out.writeInt(kbVelocity); out.writeInt(kbRemap ? 1 : 0);
}

void M1AudioProcessor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream in(data, (size_t)size, false);
    if (size < 24 || in.readInt() != 0x4D314D31 || in.readInt() != 1) return;
    const bool combi = in.readInt() != 0;
    const int idx = in.readInt();
    volumePercent = juce::jlimit(0, 100, in.readInt());
    const int memLen = in.readInt();
    combiMode = combi;
    index = juce::jlimit(0, 99, idx);
    if (ready) pendingSelect = index.load() | (combi ? 0x100 : 0);
    if (memLen == (int)sizeof(m1::MemoryImage) && in.getNumBytesRemaining() >= memLen) {
        // Loaded before the engine exists: held until prepareToPlay builds it.
        // Applied on the audio thread (applyPending) once the engine exists.
        std::lock_guard<std::mutex> lock(syxMutex);
        stateMemory.resize((size_t)memLen);
        in.read(stateMemory.data(), memLen);
        stateMemoryPending = true;
    }
    if (in.getNumBytesRemaining() >= 4) {
        const bool prot = in.readInt() != 0;
        protectFlag = prot;
        mem.protect = prot;
    }
    if (in.getNumBytesRemaining() >= 24) {   // keyboard settings (added after the protect flag)
        kbChannel = juce::jlimit(1, 16, in.readInt()); kbPcInput = in.readInt() != 0; kbPcLayout = in.readInt() != 0 ? 1 : 0;
        kbOctaves = juce::jlimit(0, 4, in.readInt()); kbVelocity = juce::jlimit(1, 127, in.readInt()); kbRemap = in.readInt() != 0;
    }
}

juce::AudioProcessorEditor* M1AudioProcessor::createEditor() { return new M1Editor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new M1AudioProcessor(); }
