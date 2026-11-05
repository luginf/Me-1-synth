// Headless checks for the Me-1 processor.
//   1. one instance at 48 kHz: program 1, note 60, blocks processed, not silent
//   2. two instances rendered interleaved give the same samples as one alone
//   3. SysEx: the factory ALL DATA dump fed back in leaves the sound unchanged
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "M1AudioProcessor.h"

static constexpr int kBlock = 256, kBlocks = 200;

static void note(juce::MidiBuffer& mb, bool on, int n = 60) {
    mb.addEvent(on ? juce::MidiMessage::noteOn(1, n, (juce::uint8)100) : juce::MidiMessage::noteOff(1, n), 0);
}

// Render kBlocks blocks; note on in block 0, off at block 100. Appends to out.
static void renderBlock(M1AudioProcessor& p, int b, std::vector<float>& out) {
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer mb;
    if (b == 0) note(mb, true);
    if (b == 100) note(mb, false);
    p.processBlock(buf, mb);
    for (int c = 0; c < 2; ++c)
        out.insert(out.end(), buf.getReadPointer(c), buf.getReadPointer(c) + kBlock);
}

static float peak(const std::vector<float>& v) {
    float m = 0;
    for (float x : v) m = std::max(m, std::abs(x));
    return m;
}

// Renders `kBlocks` blocks of one note after applying whatever the setup lambda queued.
static std::vector<float> renderNote(M1AudioProcessor& p, int noteNum) {
    std::vector<float> out;
    for (int b = 0; b < kBlocks; ++b) {
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer mb;
        if (b == 0) note(mb, true, noteNum);
        if (b == 100) note(mb, false, noteNum);
        p.processBlock(buf, mb);
        for (int c = 0; c < 2; ++c) out.insert(out.end(), buf.getReadPointer(c), buf.getReadPointer(c) + kBlock);
    }
    return out;
}

static void prep(M1AudioProcessor& p) {
    p.setPlayConfigDetails(0, 2, 48000.0, kBlock);
    p.prepareToPlay(48000.0, kBlock);
}

int main(int argc, char** argv) {
    if (const char* dir = std::getenv("M1_PROBE_LOCATE")) {   // ROM folder chosen by hand (no automatic hit expected)
        M1AudioProcessor p;
        const bool before = p.isReady();
        const auto msg = p.locateRoms(juce::File(dir));
        std::printf("locate ROMs: ready before %d, after %d (%s)\n", before, p.isReady(), msg.toRawUTF8());
        M1AudioProcessor q;   // a new instance finds the saved folder by itself
        std::printf("locate ROMs: new instance ready %d\n", q.isReady());
        return p.isReady() && !before && q.isReady() ? 0 : 1;
    }

    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;

    std::vector<float> single;
    {
        M1AudioProcessor p;
        p.setPlayConfigDetails(0, 2, 48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        if (!p.isReady()) { std::printf("FAIL: not ready: %s\n", p.statusMessage().toRawUTF8()); return 1; }
        std::printf("%s | prog 0 = '%s'\n", p.statusMessage().toRawUTF8(), p.nameAt(false, 0).toRawUTF8());
        for (int b = 0; b < kBlocks; ++b) renderBlock(p, b, single);
        const float pk = peak(single);
        std::printf("single: peak %.4f\n", pk);
        if (pk < 0.05f || pk > 1.0f) { std::printf("FAIL: peak out of range\n"); ++fails; }
    }

    {   // two instances, different programs on the second, interleaved block by block
        M1AudioProcessor a, b;
        for (auto* p : {&a, &b}) { p->setPlayConfigDetails(0, 2, 48000.0, kBlock); p->prepareToPlay(48000.0, kBlock); }
        b.requestSelect(false, 40);
        std::vector<float> oa, ob;
        for (int i = 0; i < kBlocks; ++i) { renderBlock(a, i, oa); renderBlock(b, i, ob); }
        const bool same = oa.size() == single.size() && std::memcmp(oa.data(), single.data(), oa.size() * 4) == 0;
        std::printf("two instances: A %s the lone instance, B differs: %s\n", same ? "==" : "!=",
                    (ob != oa) ? "yes" : "no");
        if (!same || ob == oa) { std::printf("FAIL: instances interfere\n"); ++fails; }
    }

    {   // sweep: every program and every combination sounds, and stays finite
        M1AudioProcessor p;
        p.setPlayConfigDetails(0, 2, 48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        int silent = 0, bad = 0;
        for (int combi = 0; combi < 2; ++combi)
            for (int i = 0; i < 100; ++i) {
                p.requestSelect(combi != 0, i);
                std::vector<float> o;
                for (int b = 0; b < 40; ++b) renderBlock(p, b == 0 ? 0 : 1, o);   // note on in the first block only
                bool finite = true;
                for (float x : o) if (!std::isfinite(x)) finite = false;
                if (!finite) { ++bad; std::printf("  NON-FINITE %s %d '%s'\n", combi ? "combi" : "prog", i, p.nameAt(combi != 0, i).toRawUTF8()); }
                else if (peak(o) < 1.0e-4f) { ++silent; std::printf("  silent %s %d '%s'\n", combi ? "combi" : "prog", i, p.nameAt(combi != 0, i).toRawUTF8()); }
                // release and let the voices die before the next one
                juce::AudioBuffer<float> buf(2, kBlock);
                juce::MidiBuffer mb; mb.addEvent(juce::MidiMessage::allNotesOff(1), 0); mb.addEvent(juce::MidiMessage::allSoundOff(1), 0);
                p.processBlock(buf, mb);
            }
        std::printf("sweep of 100 programs + 100 combis: %d silent, %d non-finite\n", silent, bad);
        if (bad) ++fails;
        if (silent) std::printf("  (silent ones are listed above; not counted as failures, see README)\n");
    }

    {   // state round trip: combi 7 saved, restored into a fresh instance before prepareToPlay
        juce::MemoryBlock st;
        std::vector<float> ref;
        {
            M1AudioProcessor a;
            a.setPlayConfigDetails(0, 2, 48000.0, kBlock);
            a.prepareToPlay(48000.0, kBlock);
            a.requestSelect(true, 7);
            juce::AudioBuffer<float> buf(2, kBlock); juce::MidiBuffer mb;
            a.processBlock(buf, mb);            // applies the selection
            a.setVolumePercent(55);
            a.editorFolded = true;
            a.getStateInformation(st);
            for (int b = 0; b < kBlocks; ++b) renderBlock(a, b, ref);
        }
        M1AudioProcessor c;
        c.setPlayConfigDetails(0, 2, 48000.0, kBlock);
        c.setStateInformation(st.getData(), (int)st.getSize());
        c.prepareToPlay(48000.0, kBlock);
        std::vector<float> o;
        {   // same empty block the reference instance processed before its render
            juce::AudioBuffer<float> buf(2, kBlock); juce::MidiBuffer mb;
            c.processBlock(buf, mb);
        }
        for (int b = 0; b < kBlocks; ++b) renderBlock(c, b, o);
        const bool okState = c.isCombiMode() && c.currentIndex() == 7 && c.getVolumePercent() == 55 && c.editorFolded;
        const bool same = o == ref;
        std::printf("state round trip: mode/index/volume %s, render %s\n", okState ? "restored" : "WRONG", same ? "identical" : "DIFFERENT");
        if (!okState || !same) ++fails;
    }

    {   // parameter editing, through the editor's request path
        using T = M1AudioProcessor::EditTarget;
        const m1::ParamDef level{"Level", 86, m1::PK::U, 0, 99, 0};
        const m1::ParamDef transp{"Key Transp", 1, m1::PK::S, -12, 12, 0};

        M1AudioProcessor base; prep(base); base.requestSelect(false, 1);
        const auto ref = renderNote(base, 60);

        M1AudioProcessor ed; prep(ed); ed.requestSelect(false, 1);
        ed.requestParam(T::Program, level, 0);
        const auto quiet = renderNote(ed, 60);
        uint8_t blk[210];
        ed.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const bool heard = blk[86] == 0 && peak(quiet) < 0.2f * peak(ref) && ed.isEdited();
        std::printf("edit OSC1 level to 0: block %d, peak %.4f vs %.4f, edited=%d -> %s\n", blk[86], peak(quiet), peak(ref), (int)ed.isEdited(), heard ? "ok" : "FAIL");
        if (!heard) ++fails;

        // write to slot 50, reselect: the edit is stored; slot 1 is untouched
        ed.requestWrite(false, 50);
        { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; ed.processBlock(b, m); }   // selections apply before queued edits
        ed.requestSelect(false, 50);
        { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; ed.processBlock(b, m); }
        ed.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const int stored = blk[86];
        ed.requestSelect(false, 1);
        { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; ed.processBlock(b, m); }
        ed.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const bool wrote = stored == 0 && blk[86] != 0 && !ed.isEdited();
        std::printf("write to slot 50: stored level %d, slot 1 level %d -> %s\n", stored, blk[86], wrote ? "ok" : "FAIL");
        if (!wrote) ++fails;

        // global key transpose +12 on note 60 == note 72 untransposed
        M1AudioProcessor g; prep(g); g.requestSelect(false, 1);
        g.requestParam(T::Global, transp, 12);
        const auto up = renderNote(g, 60);
        const auto n72 = renderNote(base, 72);
        // base already played one note; compare against a fresh instance for exactness
        M1AudioProcessor f; prep(f); f.requestSelect(false, 1);
        const auto fresh72 = renderNote(f, 72);
        const bool tr = up == fresh72 && !n72.empty();
        std::printf("global key transpose +12: note 60 %s note 72\n", tr ? "==" : "!=");
        if (!tr) ++fails;
    }

    {   // Compare and memory protect
        using T = M1AudioProcessor::EditTarget;
        const m1::ParamDef level{"Level", 86, m1::PK::U, 0, 99, 0};
        auto tick = [](M1AudioProcessor& q) { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; q.processBlock(b, m); };
        M1AudioProcessor base; prep(base); base.requestSelect(false, 1);
        const auto ref = renderNote(base, 60);

        M1AudioProcessor c; prep(c); c.requestSelect(false, 1); tick(c);
        c.requestParam(T::Program, level, 0); tick(c);
        uint8_t blk[210];
        c.requestCompare(true); tick(c);
        c.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const int shownWhileComparing = blk[86];
        c.requestParam(T::Program, level, 5); tick(c);              // refused while comparing
        const auto cmpRender = renderNote(c, 60);
        const bool cmpOk = c.isComparing() && shownWhileComparing != 0 && peak(cmpRender) > 0.5f * peak(ref);
        c.requestCompare(false); tick(c);
        c.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const bool backOk = !c.isComparing() && blk[86] == 0;       // the edit survived, the refused one never landed
        std::printf("compare: stored shown (%d), sounds like the original, edit refused; off again keeps edit (%d) -> %s\n",
                    shownWhileComparing, blk[86], (cmpOk && backOk) ? "ok" : "FAIL");
        if (!cmpOk || !backOk) ++fails;

        M1AudioProcessor pr; prep(pr); pr.requestSelect(false, 1); tick(pr);
        pr.requestParam(T::Program, level, 0); tick(pr);
        pr.requestProtect(true); tick(pr);
        pr.requestWrite(false, 60); tick(pr);
        pr.requestSelect(false, 60); tick(pr);
        pr.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const int refused = blk[86];
        pr.requestSelect(false, 1); tick(pr);
        pr.requestProtect(false); tick(pr);
        pr.requestParam(T::Program, level, 0); tick(pr);
        pr.requestWrite(false, 60); tick(pr);
        pr.requestSelect(false, 60); tick(pr);
        pr.getEditBlock(T::Program, 0, 0, blk, sizeof blk);
        const bool protOk = refused != 0 && blk[86] == 0;
        std::printf("protect: write refused (slot level %d), allowed once off (%d) -> %s\n", refused, blk[86], protOk ? "ok" : "FAIL");
        if (!protOk) ++fails;
    }

    {   // graph data
        const double fast = M1AudioProcessor::egSegmentSeconds(10, true), slow = M1AudioProcessor::egSegmentSeconds(90, true);   // a larger time value is a longer segment
        const auto lowCut = M1AudioProcessor::vdfResponseDb(20, {100.0f, 8000.0f});
        const auto hiCut = M1AudioProcessor::vdfResponseDb(95, {100.0f, 8000.0f});
        M1AudioProcessor w; prep(w);
        std::vector<float> mn, mx; int zones = 0;
        const int zone = w.multisoundWave(0, 60, 200, mn, mx, zones);
        float span = 0; for (size_t i = 0; i < mx.size(); ++i) span = std::max(span, mx[i] - mn[i]);
        const bool ok = slow > fast && fast > 0 && lowCut[1] < hiCut[1] - 6.0f && lowCut[0] > lowCut[1] && hiCut[0] > -3.0f && zone > 0 && span > 0.05f;
        std::printf("graph data: EG 10 -> %.2fs, 90 -> %.2fs; VDF 100 Hz/8 kHz at cutoff 20: %.1f/%.1f dB, 95: %.1f/%.1f dB; wave zone %d/%d span %.2f -> %s\n",
                    fast, slow, lowCut[0], lowCut[1], hiCut[0], hiCut[1], zone, zones, span, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }

    {   // on-screen keyboard host: the 16-channel broadcast collapses to one note, activity flags follow
        auto run = [&](bool broadcast, bool& activeAfterOn, bool& activeAfterOff, int& voices) {
            M1AudioProcessor q; prep(q); q.requestSelect(false, 1);
            { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; q.processBlock(b, m); }
            for (int ch = 1; ch <= (broadcast ? 16 : 1); ++ch) q.injectTestNote(ch, 60, 0.8f, true);
            std::vector<float> o;
            for (int b = 0; b < 40; ++b) { juce::AudioBuffer<float> buf(2, kBlock); juce::MidiBuffer m; q.processBlock(buf, m);
                o.insert(o.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock); }
            activeAfterOn = q.isNoteActive(60);
            voices = q.activeVoices();
            for (int ch = 1; ch <= (broadcast ? 16 : 1); ++ch) q.injectTestNote(ch, 60, 0.0f, false);
            { juce::AudioBuffer<float> buf(2, kBlock); juce::MidiBuffer m; q.processBlock(buf, m); }
            activeAfterOff = q.isNoteActive(60);
            return o;
        };
        bool onA, offA, onB, offB; int vA, vB;
        const auto a = run(true, onA, offA, vA), b = run(false, onB, offB, vB);
        const bool ok = a == b && onA && !offA && onB && !offB && vA == vB && peak(a) > 0.05f;
        std::printf("keyboard host: broadcast == single channel (%d voices), activity flags on/off -> %s  [same=%d onA=%d offA=%d onB=%d offB=%d vA=%d vB=%d peak=%.3f]\n", vA, ok ? "ok" : "FAIL", (int)(a == b), onA, offA, onB, offB, vA, vB, peak(a));
        if (!ok) ++fails;
    }

    {   // multitimbral Combination mode: each timbre answers on its own MIDI channel
        using T = M1AudioProcessor::EditTarget;
        auto tick = [](M1AudioProcessor& q) { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; q.processBlock(b, m); };
        auto onChannel = [&](M1AudioProcessor& q, int ch) {
            std::vector<float> o;
            for (int b = 0; b < 60; ++b) {
                juce::AudioBuffer<float> buf(2, kBlock); juce::MidiBuffer mb;
                if (b == 0) mb.addEvent(juce::MidiMessage::noteOn(ch, 60, (juce::uint8)100), 0);
                if (b == 30) mb.addEvent(juce::MidiMessage::noteOff(ch, 60), 0);
                q.processBlock(buf, mb);
                o.insert(o.end(), buf.getReadPointer(0), buf.getReadPointer(0) + kBlock);
            }
            // let the voices die before the next probe note
            juce::AudioBuffer<float> buf(2, kBlock); juce::MidiBuffer mb; mb.addEvent(juce::MidiMessage::allSoundOff(ch), 0); q.processBlock(buf, mb);
            return o;
        };
        // combination 7 with timbre 1 -> channel 2, timbre 2 -> channel 3, timbre 3 -> channel 1 (windows opened)
        auto fresh = [&](int ch) {
            M1AudioProcessor q; prep(q); q.requestSelect(true, 7); tick(q);
            auto set = [&](int off, m1::PK k, int lo, int hi, int v) { q.requestParam(T::Combi, m1::ParamDef{"x", (uint8_t)off, k, (int16_t)lo, (int16_t)hi, 0}, v); };
            set(46, m1::PK::Field, 1, 16, 2);           // timbre 1 channel 2
            set(46 + 11, m1::PK::Field, 1, 16, 3);      // timbre 2 channel 3
            set(46 + 22, m1::PK::Field, 1, 16, 1);      // timbre 3 channel 1
            set(41 + 22, m1::PK::Note, 0, 127, 127); set(42 + 22, m1::PK::Note, 0, 127, 0);   // key window wide open
            set(43 + 22, m1::PK::U, 1, 127, 127); set(44 + 22, m1::PK::U, 1, 127, 1);          // velocity window wide open
            set(46 + 33, m1::PK::Field, 1, 16, 4);      // timbre 4 channel 4
            tick(q);
            return onChannel(q, ch);
        };
        const auto c1 = fresh(1), c2 = fresh(2), c3 = fresh(3), c5 = fresh(5);
        const bool ok = peak(c1) > 0.01f && peak(c2) > 0.01f && peak(c3) > 0.01f && peak(c5) < 1.0e-6f && c2 != c3 && c1 != c2;
        std::printf("multitimbral: combi 7 peaks ch1 %.3f, ch2 %.3f, ch3 %.3f, ch5 (no timbre) %.6f -> %s\n", peak(c1), peak(c2), peak(c3), peak(c5), ok ? "ok" : "FAIL");
        if (!ok) ++fails;
        // Program mode is still omni
        M1AudioProcessor pq; prep(pq); pq.requestSelect(false, 1); tick(pq);
        const auto p5 = onChannel(pq, 5);
        std::printf("program mode on channel 5: peak %.3f (omni) -> %s\n", peak(p5), peak(p5) > 0.05f ? "ok" : "FAIL");
        if (!(peak(p5) > 0.05f)) ++fails;
    }

    if (argc > 1) {   // SysEx round trip with the factory dump
        M1AudioProcessor p;
        p.setPlayConfigDetails(0, 2, 48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        std::printf("%s\n", p.importSyx(juce::File(argv[1])).toRawUTF8());
        std::vector<float> o;
        for (int b = 0; b < kBlocks; ++b) renderBlock(p, b, o);
        const bool same = o.size() == single.size() && std::memcmp(o.data(), single.data(), o.size() * 4) == 0;
        std::printf("after reloading the factory dump: %s; sysex received %u, errors %u\n",
                    same ? "identical" : "DIFFERENT", p.sysexReceived(), p.sysexErrors());
        if (!same || p.sysexReceived() == 0 || p.sysexErrors() != 0) ++fails;
    }
    if (argc > 2) {   // offscreen snapshots of the editor (no display needed): argv[2] = output prefix
        M1AudioProcessor p;
        prep(p);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto tick = [&] { juce::AudioBuffer<float> b(2, kBlock); juce::MidiBuffer m; p.processBlock(b, m); };
        auto snap = [&](const char* name) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(350);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            juce::File f(juce::String(argv[2]) + name + ".png");
            juce::FileOutputStream os(f);
            if (os.openedOk()) { os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
            std::printf("snapshot %s\n", f.getFullPathName().toRawUTF8());
        };
        snap("prog");
        p.requestSelect(true, 7); tick(); snap("combi");
    }
    std::printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
