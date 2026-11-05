#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
// MidiMessageCollector lives here, not in juce_audio_processors.
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "D110KeyboardHost.h"
#include "M1_SysEx.h"
#include "m1_params.h"
#include "m1_engine/m1_engine.h"
#include "m1_memory.h"

// Korg M1 plugin around the vendored PicoFaceM1 engine (m1_engine/).
//
// Deliberately does NOT use M1_Bridge: that class keeps the effect delay RAM,
// the user memory and the kernel copies in file-scope globals, so two plugin
// instances in one host would share them. Everything stateful lives here, per
// instance (m1::Engine, fxMem, m1::Memory). Only the read-only ROM tables
// (m1rom::k*, generated at configure time) are shared.
//
// The engine renders float stereo at m1::kSampleRate (31.25 kHz) and is
// resampled to the host rate. The 4 MB PCM is read from m1_pcm.bin at runtime
// (see loadPcm()); it is not embedded in the binary. The other ROM-derived
// tables (kernel, VDF, factory programs...) ARE compiled in, so a build is for
// the owner of the ROMs only and must not be redistributed.
class M1AudioProcessor : public juce::AudioProcessor, public D110KeyboardHost, private M1_SysExHost {
public:
    M1AudioProcessor();
    ~M1AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Me-1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // --- for the editor (message thread) -----------------------------------
    bool isReady() const { return ready; }
    juce::String statusMessage() const { return status; }
    bool isCombiMode() const { return combiMode.load(); }
    int  currentIndex() const { return index.load(); }
    juce::String nameAt(bool combi, int i) const;
    // Requests are applied on the audio thread at the start of the next block.
    void requestSelect(bool combi, int i);
    int  getVolumePercent() const { return volumePercent.load(); }
    void setVolumePercent(int v) { volumePercent = juce::jlimit(0, 100, v); }
    // SysEx messages accepted / rejected so far (diagnostics, any thread: racy but monotonic).
    unsigned sysexReceived() const { return sysex ? sysex->received() : 0; }
    unsigned sysexErrors() const { return sysex ? sysex->errors() : 0; }
    int  activeVoices() const { return activeVoicesShown.load(); }
    // --- parameter editing (message thread) ---------------------------------
    // The editor works directly on the parameter tables (m1_params.h); nothing
    // here goes through the firmware's encoder/page controller. Changes are
    // queued and applied on the audio thread at the start of the next block.
    enum class EditTarget { Program, Combi, Global, Kit };
    void requestParam(EditTarget t, const m1::ParamDef& p, int value, int kit = 0, int inst = 0);
    void requestWrite(bool combi, int dest);   // edit buffer -> user memory slot
    void requestRevert();                      // reload the edit buffer from memory
    // Copy of the block a target edits (program 143, combi 124, global 21, kit instrument 7).
    void getEditBlock(EditTarget t, int kit, int inst, uint8_t* out, size_t cap) const;
    int  editRevision() const { return editRev.load(); }
    bool isEdited() const { return editedFlag.load(); }
    // Compare: play (and show) the stored version of the current Program/Combination;
    // edits are refused while it is on. Protect: refuses Write (the M1's memory protect).
    void requestCompare(bool on);
    void requestProtect(bool on);
    bool isComparing() const { return comparing.load(); }
    bool isProtected() const { return protectFlag.load(); }

    // --- data for the graphs (message thread; read-only tables and this instance's PCM) ---
    // Duration of one EG segment for a 0..99 time parameter (engine's reference formula, no key/velocity scaling).
    static double egSegmentSeconds(int timeParam, bool rising);
    // VDF response in dB for a 0..99 cutoff parameter at the given frequencies (both chips, four poles).
    static std::vector<float> vdfResponseDb(int cutoffParam, const std::vector<float>& freqsHz);
    // Waveform of a multisound's zone for `key`: per-column min/max in -1..1. Returns the zone number
    // (1-based) and sets zones, or 0 if there is nothing to draw.
    int multisoundWave(int multisound, int key, int columns, std::vector<float>& mins, std::vector<float>& maxs, int& zones) const;
    // Display text of a parameter in a given block (names resolved from this instance's memory).
    juce::String formatParam(const m1::ParamDef& p, const uint8_t* block) const;
    // --- D110KeyboardHost: the on-screen keyboard shared with the D-110/D-50/JV projects ---
    // The engine is monotimbral and channel-blind (omni), so channel/remap only matter to what the
    // keyboard itself shows; see injectTestNote() for how the 16-channel broadcast is collapsed.
    void injectTestNote(int channel, int note, float velocity, bool on) override;
    int getKeyboardMidiChannel() const override { return kbChannel; }
    void setKeyboardMidiChannel(int c) override { kbChannel = c; }
    bool getMidiRemap() const override { return kbRemap; }
    void setMidiRemap(bool r) override { kbRemap = r; }
    bool getKeyboardPcInputEnabled() const override { return kbPcInput; }
    void setKeyboardPcInputEnabled(bool e) override { kbPcInput = e; }
    int getKeyboardPcLayout() const override { return kbPcLayout; }
    void setKeyboardPcLayout(int l) override { kbPcLayout = l; }
    int getKeyboardNumOctaves() const override { return kbOctaves; }
    void setKeyboardNumOctaves(int n) override { kbOctaves = n; }
    int getKeyboardVelocity() const override { return kbVelocity; }
    void setKeyboardVelocity(int v) override { kbVelocity = juce::jlimit(1, 127, v); }
    bool isNoteActive(int note) const override {
        return note >= 0 && note < 128 && noteActive[(size_t)note].load(std::memory_order_relaxed);
    }
    // Feed a raw SysEx file (e.g. an ALL DATA dump) to the engine. Returns a
    // message for the caller to show.
    juce::String importSyx(const juce::File& file);

    // Engine-only entry point for headless tests: no MIDI buffer, no state.
    void renderForTest(float* l, float* r, int n) { renderInternal(n, l, r); }

private:
    // M1_SysExHost ------------------------------------------------------------
    int  m1Mode() const override { return combiMode.load() ? 0 : 2; }
    void setM1Mode(int m) override;
    uint8_t* programEdit() override { return progEdit; }
    uint8_t* combiEdit() override { return combiEditBuf; }
    void programEditLoaded() override;
    void combiEditLoaded() override;
    bool writeProgram(int n) override;
    bool writeCombi(int n) override;
    void memoryLoaded() override;
    bool parameterChange(int, int, int) override { return false; }   // not supported yet
    int  globalChannel() const override { return 16; }                // any channel
    m1::Memory& memory() override { return mem; }

    bool loadPcm();
    void initEngine();
    void applyPending();
    void handleMidi(const juce::MidiMessage&);
    void renderInternal(int n, float* l, float* r);
    void select(bool combi, int i);
    struct EditCmd { int kind; EditTarget target; m1::ParamDef p; int value; int kit, inst, dest; bool combi; };
    void applyEdit(const EditCmd&);
    uint8_t* blockFor(EditTarget, int kit, int inst);
    void applyGlobal();

    // One engine per instance.
    std::unique_ptr<m1::Engine> engine;
    std::vector<int16_t> pcm;
    std::array<int16_t, 65536> fxMem{};
    m1::Memory mem;
    uint8_t progEdit[m1::kProgLen]{};
    uint8_t combiEditBuf[m1::kCombiLen]{};
    std::unique_ptr<M1_SysEx> sysex;

    bool ready = false;
    juce::String status;
    std::atomic<bool> combiMode{false};
public:
    bool editorFolded = false;     // editor section hidden (compact synth), per instance, saved in the state
private:
    std::atomic<int> index{0};
    std::atomic<int> volumePercent{80};
    std::atomic<int> activeVoicesShown{0};
    std::atomic<int> pendingSelect{-1};      // index | (combi ? 0x100 : 0)
    float midiGain = 1.0f;

    juce::MidiMessageCollector injectedMidi;
    // on-screen keyboard settings (message thread), saved with the state
    int kbChannel = 1, kbPcLayout = 0, kbOctaves = 2, kbVelocity = 100;
    bool kbRemap = false, kbPcInput = false;
    std::array<std::atomic<bool>, 128> noteActive{};
    std::mutex syxMutex;
    std::vector<uint8_t> pendingSyx;
    mutable std::mutex editMutex;            // guards the edit buffers and memory image copies
    std::mutex cmdMutex;
    std::vector<EditCmd> cmds;
    std::atomic<int> editRev{0};
    std::atomic<bool> editedFlag{false};
    std::atomic<bool> comparing{false};
    std::atomic<bool> protectFlag{false};
    std::vector<uint8_t> stateMemory;       // user memory restored by setStateInformation
    std::atomic<bool> stateMemoryPending{false};

    double hostSampleRate = 48000.0;
    juce::LagrangeInterpolator resamplerL, resamplerR;
    std::vector<float> pendingL, pendingR, scratchL, scratchR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(M1AudioProcessor)
};
