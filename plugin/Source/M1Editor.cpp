#include "M1Editor.h"

#include <cmath>
#include <map>

namespace {
enum GraphKind { kNoGraph = 0, kPitchEg, kVdfEg, kVdaEg, kVdfResp, kWave };

int graphKindFor(const juce::String& t) {
    if (t.startsWith("Pitch EG")) return kPitchEg;
    if (t.startsWith("VDF") && t.endsWith("EG")) return kVdfEg;
    if (t.startsWith("VDA") && t.endsWith("EG")) return kVdaEg;
    if (t == "VDF 1" || t == "VDF 2") return kVdfResp;
    if (t == "OSC 1" || t == "OSC 2") return kWave;
    return kNoGraph;
}

// Classic 5 x 7 dot-matrix font, ASCII 32..126, one byte per column, bit 0 = top row.
const uint8_t kFont5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50},
    {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00},
    {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x10, 0x08, 0x08, 0x10, 0x08}
};
}  // namespace

//==============================================================================
// An imitation of the M1's LCD: a dot-matrix screen of 40 x 8 characters (6 x 8
// dots each, 240 x 64 dots), drawn as lit squares. The page layouts are our own,
// built from this editor's state; the original screens are produced by the V50
// firmware, which is not emulated. Glyphs are rasterised once from a monospace
// font and thresholded to 1 bit.
class M1Editor::LcdView : public juce::Component {
public:
    static constexpr int kCols = 40, kRows = 8, kCw = 6, kCh = 8, kScale = 2;
    struct Line { juce::String text; int invFrom = 0, invLen = 0; };

    LcdView() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void setLines(const std::array<Line, kRows>& l) {
        bool same = true;
        for (int i = 0; i < kRows; ++i)
            if (l[(size_t)i].text != lines[(size_t)i].text || l[(size_t)i].invFrom != lines[(size_t)i].invFrom || l[(size_t)i].invLen != lines[(size_t)i].invLen) same = false;
        if (same) return;
        lines = l;
        rebuild();
        repaint();
    }
    std::function<void()> onClick;
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff0d1a14));
        if (image.isValid()) g.drawImageAt(image, 8, 8);
        g.setColour(juce::Colour(0xff2a3a32));
        g.drawRect(getLocalBounds(), 2);
    }
    static int width() { return kCols * kCw * kScale + 16; }
    static int height() { return kRows * kCh * kScale + 16; }

private:
    const std::array<bool, kCw * kCh>& glyph(juce::juce_wchar c) {
        auto it = cache.find(c);
        if (it != cache.end()) return it->second;
        const int code = (c >= 32 && c <= 126) ? (int)c - 32 : '?' - 32;
        std::array<bool, kCw * kCh> bits{};
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 7; ++y) bits[(size_t)(y * kCw + x)] = (kFont5x7[code][x] >> y) & 1;
        return cache.emplace(c, bits).first->second;
    }

    void rebuild() {
        image = juce::Image(juce::Image::ARGB, kCols * kCw * kScale, kRows * kCh * kScale, true);
        juce::Graphics g(image);
        const auto bg = juce::Colour(0xff9db894), on = juce::Colour(0xff10281c);
        g.fillAll(bg);
        for (int r = 0; r < kRows; ++r) {
            const auto& ln = lines[(size_t)r];
            for (int c = 0; c < kCols; ++c) {
                const bool inv = c >= ln.invFrom && c < ln.invFrom + ln.invLen;
                const juce::juce_wchar ch = c < ln.text.length() ? ln.text[c] : ' ';
                const auto& bits = ch == ' ' ? emptyGlyph : glyph(ch);
                for (int y = 0; y < kCh; ++y)
                    for (int x = 0; x < kCw; ++x) {
                        const bool lit = bits[(size_t)(y * kCw + x)] != inv;
                        if (!lit) continue;
                        g.setColour(on);
                        g.fillRect((c * kCw + x) * kScale, (r * kCh + y) * kScale, kScale, kScale);
                    }
            }
        }
    }

    std::array<Line, kRows> lines;
    std::map<juce::juce_wchar, std::array<bool, kCw * kCh>> cache;
    std::array<bool, kCw * kCh> emptyGlyph{};
    juce::Image image;
};

//==============================================================================
// Envelope, filter-response and waveform views for the current page. Values come
// from the edit buffer (by parameter name); the shapes follow the engine: segment
// durations from its EG rate tables, the VDF response from its coefficient
// tables, the waveform from the multisound's PCM. Times of key scaling and
// velocity are not applied: it is the static shape of the page.
class M1Editor::GraphView : public juce::Component {
public:
    explicit GraphView(M1AudioProcessor& p) : proc(p) {}
    void setData(int k, const std::map<juce::String, int>& v, int multisound, int key) {
        if (k == kind && v == vals && multisound == ms && key == wkey) return;
        kind = k; vals = v; ms = multisound; wkey = key;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff15151a));
        g.fillRoundedRectangle(r, 4.0f);
        r = r.reduced(8.0f, 6.0f);
        g.setColour(juce::Colours::grey);
        g.setFont(12.0f);
        switch (kind) {
            case kPitchEg: case kVdfEg: case kVdaEg: paintEg(g, r); break;
            case kVdfResp: paintVdf(g, r); break;
            case kWave:    paintWave(g, r); break;
            default: break;
        }
    }
private:
    int v(const char* n, int d = 0) const { auto it = vals.find(n); return it == vals.end() ? d : it->second; }

    void paintEg(juce::Graphics& g, juce::Rectangle<float> r) {
        const bool pitch = kind == kPitchEg, vda = kind == kVdaEg;
        const float lo = vda ? 0.0f : -99.0f, hi = 99.0f;
        struct Pt { float level; double secs; };
        std::vector<Pt> pts;   // each point: level reached, and the seconds the segment to it takes
        std::vector<juce::String> labels;
        auto seg = [&](float from, float to, int t, const char* name) {
            const double s = M1AudioProcessor::egSegmentSeconds(t, pitch ? true : to >= from);
            pts.push_back({to, s});
            labels.push_back(juce::String(name) + " " + juce::String(s, s < 1.0 ? 2 : 1) + "s");
        };
        float cur;
        int keyoffAfter;
        if (pitch) {
            cur = (float)v("Start Lvl");
            pts.push_back({cur, 0.0});
            seg(cur, (float)v("Atk Level"), v("Atk Time"), "A"); cur = (float)v("Atk Level");
            seg(cur, 0.0f, v("Dcy Time"), "D"); cur = 0.0f;
            keyoffAfter = (int)pts.size();
            seg(cur, (float)v("Rel Level"), v("Rel Time"), "R");
        } else {
            cur = 0.0f;
            pts.push_back({0.0f, 0.0});
            seg(cur, (float)v("Atk Level"), v("Atk Time"), "A"); cur = (float)v("Atk Level");
            seg(cur, (float)v("Break Pt"), v("Dcy Time"), "D"); cur = (float)v("Break Pt");
            seg(cur, (float)v("Sustain"), v("Slope Tm"), "S"); cur = (float)v("Sustain");
            keyoffAfter = (int)pts.size();
            seg(cur, vda ? 0.0f : (float)v("Rel Level"), v("Rel Time"), "R");
        }
        // x: compressed durations, a fixed-width hold before key-off
        const double hold = 0.0;
        std::vector<double> w;
        double total = 0.0;
        for (size_t i = 0; i < pts.size(); ++i) {
            double d = std::sqrt(pts[i].secs) + 0.15 * (i > 0 ? 1 : 0);
            if ((int)i == keyoffAfter) { w.push_back(0.5); total += 0.5; }   // key-off hold
            w.push_back(d);
            total += d;
        }
        (void)hold;
        const float x0 = r.getX(), W = r.getWidth(), y0 = r.getY(), H = r.getHeight() - 14.0f;
        auto Y = [&](float l) { return y0 + H * (1.0f - (l - lo) / (hi - lo)); };
        g.setColour(juce::Colour(0xff2c2c34));
        g.drawHorizontalLine((int)Y(0.0f), x0, x0 + W);
        juce::Path path;
        double acc = 0.0;
        size_t wi = 0;
        float px = x0, py = Y(pts[0].level);
        path.startNewSubPath(px, py);
        float keyX = -1.0f;
        for (size_t i = 1; i < pts.size(); ++i) {
            if ((int)i == keyoffAfter) {
                acc += w[wi++];
                px = x0 + (float)(acc / total) * W;
                path.lineTo(px, py);
                keyX = px;
            }
            acc += w[wi++];
            px = x0 + (float)(acc / total) * W;
            py = Y(pts[i].level);
            path.lineTo(px, py);
        }
        g.setColour(juce::Colour(0xff4aa3d8));
        g.strokePath(path, juce::PathStrokeType(2.0f));
        if (keyX >= 0.0f) {
            g.setColour(juce::Colours::orange.withAlpha(0.6f));
            g.drawVerticalLine((int)keyX, y0, y0 + H);
            g.setFont(11.0f);
            g.drawText("key off", (int)keyX + 3, (int)y0, 50, 12, juce::Justification::left);
        }
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        juce::String text;
        for (auto& l : labels) text << l << "   ";
        g.drawText(text, (int)x0, (int)(y0 + H + 2), (int)W, 12, juce::Justification::left);
    }

    void paintVdf(juce::Graphics& g, juce::Rectangle<float> r) {
        std::vector<float> freqs;
        const int n = 160;
        const float fLo = 20.0f, fHi = 16000.0f;
        for (int i = 0; i < n; ++i) freqs.push_back(fLo * std::pow(fHi / fLo, (float)i / (float)(n - 1)));
        const float dbHi = 6.0f, dbLo = -60.0f;
        const float x0 = r.getX(), W = r.getWidth(), y0 = r.getY(), H = r.getHeight() - 14.0f;
        auto Y = [&](float db) { return y0 + H * (1.0f - (juce::jlimit(dbLo, dbHi, db) - dbLo) / (dbHi - dbLo)); };
        g.setColour(juce::Colour(0xff2c2c34));
        for (float db : {0.0f, -12.0f, -24.0f, -36.0f, -48.0f}) g.drawHorizontalLine((int)Y(db), x0, x0 + W);
        for (float f : {100.0f, 1000.0f, 10000.0f}) {
            const float x = x0 + W * std::log(f / fLo) / std::log(fHi / fLo);
            g.drawVerticalLine((int)x, y0, y0 + H);
            g.setColour(juce::Colours::grey);
            g.setFont(11.0f);
            g.drawText(f >= 1000.0f ? juce::String((int)(f / 1000.0f)) + "k" : juce::String((int)f), (int)x + 2, (int)(y0 + H + 1), 30, 12, juce::Justification::left);
            g.setColour(juce::Colour(0xff2c2c34));
        }
        auto curve = [&](int cutoff, juce::Colour c, bool dashed) {
            const auto db = M1AudioProcessor::vdfResponseDb(cutoff, freqs);
            juce::Path path;
            for (int i = 0; i < n; ++i) {
                const float x = x0 + W * (float)i / (float)(n - 1);
                if (i == 0) path.startNewSubPath(x, Y(db[(size_t)i])); else path.lineTo(x, Y(db[(size_t)i]));
            }
            g.setColour(c);
            if (dashed) {
                juce::Path d;
                const float dash[] = {5.0f, 4.0f};
                juce::PathStrokeType(1.5f).createDashedStroke(d, path, dash, 2);
                g.fillPath(d);
            } else {
                g.strokePath(path, juce::PathStrokeType(2.0f));
            }
        };
        const int cut = v("Cutoff"), peak = juce::jmin(99, cut + v("EG Int"));
        if (peak > cut) curve(peak, juce::Colour(0xffe0a040), true);
        curve(cut, juce::Colour(0xff4aa3d8), false);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        g.drawText("VDF response: cutoff " + juce::String(cut) + (peak > cut ? " (solid), cutoff + EG Int " + juce::String(peak) + " (dashed, approximate peak)" : ""),
                   (int)x0 + 4, (int)y0 + 2, (int)W - 8, 14, juce::Justification::topLeft);
    }

    void paintWave(juce::Graphics& g, juce::Rectangle<float> r) {
        std::vector<float> mins, maxs;
        int zones = 0;
        const int cols = juce::jmax(8, (int)r.getWidth());
        const int zone = proc.multisoundWave(ms, wkey, cols, mins, maxs, zones);
        const float H = r.getHeight() - 14.0f, mid = r.getY() + H * 0.5f;
        g.setColour(juce::Colour(0xff2c2c34));
        g.drawHorizontalLine((int)mid, r.getX(), r.getRight());
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        if (zone == 0) { g.drawText("no waveform (drum program or no data)", r.toNearestInt(), juce::Justification::centred); return; }
        g.setColour(juce::Colour(0xff4aa3d8));
        for (int x = 0; x < cols; ++x)
            g.drawVerticalLine((int)r.getX() + x, mid - maxs[(size_t)x] * H * 0.5f, mid - mins[(size_t)x] * H * 0.5f + 1.0f);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("multisound " + juce::String(ms) + ", zone " + juce::String(zone) + "/" + juce::String(zones) + " for key " + juce::String(wkey) + " (whole sample)",
                   (int)r.getX(), (int)(r.getY() + H + 1), (int)r.getWidth(), 12, juce::Justification::left);
    }

    M1AudioProcessor& proc;
    int kind = kNoGraph, ms = 0, wkey = 60;
    std::map<juce::String, int> vals;
};

//==============================================================================
// One parameter: a name, and a toggle (bit kinds) or an integer slider whose
// text comes from the engine's own formatter. Values flow one way: the slider
// sends a request to the audio thread; the displayed value is re-read from the
// edit buffer by the editor's timer.
class M1Editor::ParamRow : public juce::Component {
public:
    ParamRow(M1Editor& o, const m1::ParamDef& pd) : owner(o), p(pd) {
        label.setText(p.name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
        toggle = p.kind == m1::PK::Bit || p.kind == m1::PK::Toggle || p.kind == m1::PK::Placement;
        if (toggle) {
            button.onClick = [this] { owner.sendParam(p, button.getToggleState() ? 1 : 0); };
            addAndMakeVisible(button);
        } else {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setRange((double)p.lo, (double)p.hi, 1.0);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 96, 20);
            slider.setScrollWheelEnabled(false);
            slider.textFromValueFunction = [this](double v) {
                uint8_t tmp[210];
                std::memcpy(tmp, owner.block, sizeof tmp);
                m1::paramSet(p, tmp, (int)std::lround(v));
                return owner.proc.formatParam(p, tmp);
            };
            slider.onValueChange = [this] { owner.sendParam(p, (int)std::lround(slider.getValue())); };
            addAndMakeVisible(slider);
        }
    }
    void refresh() {
        const int v = m1::paramGet(p, owner.block);
        if (toggle) {
            button.setToggleState(v != 0, juce::dontSendNotification);
        } else if (!slider.isMouseButtonDown() && (int)std::lround(slider.getValue()) != v) {
            slider.setValue((double)v, juce::dontSendNotification);
            slider.updateText();
        } else if (!slider.isMouseButtonDown()) {
            slider.updateText();
        }
    }
    void resized() override {
        auto r = getLocalBounds();
        label.setBounds(r.removeFromLeft(110));
        if (toggle) button.setBounds(r.removeFromLeft(40)); else slider.setBounds(r);
    }
private:
    M1Editor& owner;
    m1::ParamDef p;
    bool toggle = false;
    juce::Label label;
    juce::ToggleButton button;
    juce::Slider slider;
};

// The rows of the selected page, or the name editor.
class M1Editor::ParamPanel : public juce::Component {
public:
    explicit ParamPanel(M1Editor& o) : owner(o) {
        nameEditor.setInputRestrictions(10);
        nameEditor.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        nameEditor.onTextChange = [this] {
            if (updating) return;
            const auto txt = nameEditor.getText().paddedRight(' ', 10);
            for (int i = 0; i < 10; ++i) {
                const int ch = juce::jlimit(32, 126, (int)txt[i]);
                m1::ParamDef pd{"Char", (uint8_t)i, m1::PK::Char, 32, 126, 0};
                owner.sendParam(pd, ch);
            }
        };
    }
    void show(const Group& g) {
        rows.clear();
        removeAllChildren();
        isName = g.isName;
        if (isName) {
            addAndMakeVisible(nameEditor);
        } else {
            for (const auto& pd : g.params) {
                auto* r = new ParamRow(owner, pd);
                rows.add(r);
                addAndMakeVisible(r);
            }
        }
        layoutRows();
        refresh();
    }
    void refresh() {
        for (auto* r : rows) r->refresh();
        if (isName) {
            juce::String s;
            for (int i = 0; i < 10; ++i) s << juce::String::charToString((juce::juce_wchar)juce::jlimit(32, 126, (int)owner.block[i]));
            if (!nameEditor.hasKeyboardFocus(true) && nameEditor.getText() != s.trimEnd()) {
                updating = true;
                nameEditor.setText(s.trimEnd(), false);
                updating = false;
            }
        }
    }
    void resized() override { layoutRows(); }
private:
    static constexpr int kRowH = 26;
    void layoutRows() {
        const int w = getWidth();
        if (isName) { nameEditor.setBounds(8, 8, 220, 32); setSize(w, 60); return; }
        int y = 4;
        for (auto* r : rows) { r->setBounds(8, y, juce::jmax(100, w - 24), kRowH - 2); y += kRowH; }
        setSize(w, juce::jmax(y + 4, 20));
    }
    M1Editor& owner;
    juce::OwnedArray<ParamRow> rows;
    juce::TextEditor nameEditor;
    bool isName = false, updating = false;
};

//==============================================================================
M1Editor::M1Editor(M1AudioProcessor& p) : juce::AudioProcessorEditor(&p), proc(p), keyboard(p) {
    setSize(900, 876);
    for (auto* b : {&progButton, &combiButton, &importButton, &editTab, &globalTab, &kitTab, &writeButton, &revertButton})
        addAndMakeVisible(*b);
    for (auto* b : {&editTab, &globalTab, &kitTab}) b->setClickingTogglesState(false);
    progButton.onClick = [this] { lcdPlay = true; proc.requestSelect(false, proc.currentIndex()); };
    combiButton.onClick = [this] { lcdPlay = true; proc.requestSelect(true, proc.currentIndex()); };
    importButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Import an M1 SysEx dump", juce::File(), "*.syx;*.SYX");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) {
                                 const auto f = fc.getResult();
                                 if (f != juce::File()) statusLabel.setText(proc.importSyx(f), juce::dontSendNotification);
                             });
    };
    auto setTab = [this](int t) { tab = t; shownSignature.clear(); selectedGroup = 0; };
    editTab.onClick = [setTab] { setTab(0); };
    globalTab.onClick = [setTab] { setTab(1); };
    kitTab.onClick = [setTab] { setTab(2); };

    addAndMakeVisible(selector);
    selector.onChange = [this] {
        const int i = selector.getSelectedItemIndex();
        if (i >= 0 && i != shownIndex) { lcdPlay = true; proc.requestSelect(proc.isCombiMode(), i); }
    };
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    volume.setRange(0, 100, 1);
    volume.setValue(proc.getVolumePercent(), juce::dontSendNotification);
    volume.onValueChange = [this] { proc.setVolumePercent((int)volume.getValue()); };
    addAndMakeVisible(volume);
    for (auto* l : {&volumeLabel, &statusLabel, &voicesLabel, &editedLabel, &writeLabel}) addAndMakeVisible(*l);
    editedLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    statusLabel.setText(proc.statusMessage(), juce::dontSendNotification);

    for (auto* s : {&kitSlider, &instSlider}) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
        s->onValueChange = [this] { shownSignature.clear(); };
        addChildComponent(*s);
    }
    kitSlider.setRange(1, 4, 1);
    instSlider.setRange(1, 30, 1);

    addAndMakeVisible(compareButton);
    addAndMakeVisible(protectButton);
    compareButton.setClickingTogglesState(false);
    protectButton.setClickingTogglesState(false);
    compareButton.setTooltip("Play and show the stored version; edits are refused meanwhile");
    protectButton.setTooltip("Memory protect: Write is refused");
    compareButton.onClick = [this] { proc.requestCompare(!proc.isComparing()); };
    protectButton.onClick = [this] { proc.requestProtect(!proc.isProtected()); };
    lcd = std::make_unique<LcdView>();
    lcd->onClick = [this] { lcdPlay = !lcdPlay; updateLcd(); };
    addAndMakeVisible(*lcd);
    graphView = std::make_unique<GraphView>(proc);
    addChildComponent(*graphView);

    addAndMakeVisible(destBox);
    writeButton.onClick = [this] {
        const int d = destBox.getSelectedItemIndex();
        if (d >= 0) proc.requestWrite(proc.isCombiMode(), d);
    };
    revertButton.onClick = [this] { proc.requestRevert(); };

    pageList.setRowHeight(22);
    pageList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff25252b));
    addAndMakeVisible(pageList);
    panel = std::make_unique<ParamPanel>(*this);
    viewport.setViewedComponent(panel.get(), false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    addAndMakeVisible(keyboard);
    fillList();
    startTimerHz(15);
}

M1Editor::~M1Editor() {
    stopTimer();
}

M1Editor::Target M1Editor::currentTarget() const {
    if (tab == 1) return Target::Global;
    if (tab == 2) return Target::Kit;
    return proc.isCombiMode() ? Target::Combi : Target::Program;
}

void M1Editor::fillList() {
    selector.clear(juce::dontSendNotification);
    destBox.clear(juce::dontSendNotification);
    const bool combi = proc.isCombiMode();
    for (int i = 0; i < 100; ++i) {
        const auto t = juce::String(i).paddedLeft('0', 2) + "  " + proc.nameAt(combi, i);
        selector.addItem(t, i + 1);
        destBox.addItem(t, i + 1);
    }
    shownCombi = combi;
    shownIndex = proc.currentIndex();
    selector.setSelectedItemIndex(shownIndex, juce::dontSendNotification);
    destBox.setSelectedItemIndex(shownIndex, juce::dontSendNotification);
}

void M1Editor::readBlock() {
    proc.getEditBlock(currentTarget(), (int)kitSlider.getValue() - 1, (int)instSlider.getValue() - 1, block, sizeof block);
}

// What decides the shape of the page list: target, Single/Double (program),
// the two effect types, kit and instrument.
juce::String M1Editor::structureSignature() const {
    const auto t = currentTarget();
    juce::String s = juce::String((int)t) + ":" + juce::String((int)kitSlider.getValue()) + ":" + juce::String((int)instSlider.getValue());
    if (t == Target::Program) s << ":" << (int)block[10] << ":" << (int)block[m1::kProgFx] << ":" << (int)block[m1::kProgFx + 1];
    if (t == Target::Combi) s << ":" << (int)block[m1::kCombiFx] << ":" << (int)block[m1::kCombiFx + 1];
    return s;
}

// The page layout of the firmware's M1_Controller::buildEdit, without its pairing into encoders.
void M1Editor::rebuildGroups() {
    groups.clear();
    const auto t = currentTarget();
    int n = 0;
    const m1::PageDef* pages = nullptr;
    switch (t) {
        case Target::Program: pages = m1::programPages(n); break;
        case Target::Combi:   pages = m1::combiPages(n); break;
        case Target::Global:  pages = m1::globalPages(n); break;
        case Target::Kit:     pages = m1::kitPages(n); break;
    }
    const int fxBase = t == Target::Program ? m1::kProgFx : m1::kCombiFx;
    auto addParams = [](Group& g, const m1::ParamDef& pd, int dOff) {
        if (!pd.name) return;
        m1::ParamDef q = pd;
        q.offset = (uint8_t)(pd.offset + dOff);
        g.params.push_back(q);
    };
    for (int i = 0; i < n; ++i) {
        const m1::PageDef& pg = pages[i];
        if (t == Target::Program && (pg.flags & m1::kPageDoubleOnly) && block[10] != 1) continue;
        if (pg.flags & (m1::kPageFx1 | m1::kPageFx2)) {
            const int k = (pg.flags & m1::kPageFx1) ? 0 : 1;
            const int type = block[fxBase + k];
            Group g;
            g.title = "FX" + juce::String(k + 1) + " " + m1::kFxNames[type < 34 ? type : 33];
            const m1::ParamDef* fp = m1::effectParams(type);
            for (int j = 0; j < 8; ++j) addParams(g, fp[j], fxBase + 9 + 8 * k);
            groups.push_back(std::move(g));
            continue;
        }
        if (pg.flags & m1::kPageTimbre) {
            for (int tm = 0; tm < 8; ++tm) {
                Group g;
                g.title = "Timbre " + juce::String(tm + 1);
                for (int j = 0; j < pg.count; ++j) addParams(g, pg.params[j], 11 * tm);
                groups.push_back(std::move(g));
            }
            continue;
        }
        Group g;
        g.title = pg.title;
        g.graph = graphKindFor(g.title);
        g.isName = juce::String(pg.title) == "Name";
        if (!g.isName) for (int j = 0; j < pg.count; ++j) addParams(g, pg.params[j], 0);
        groups.push_back(std::move(g));
    }
    pageList.updateContent();
}

void M1Editor::showGroup(int index) {
    if (groups.empty()) return;
    selectedGroup = juce::jlimit(0, (int)groups.size() - 1, index);
    panel->setSize(viewport.getMaximumVisibleWidth(), 100);
    panel->show(groups[(size_t)selectedGroup]);
    pageList.selectRow(selectedGroup, false, false);
    resized();
    updateGraph();
}

void M1Editor::updateGraph() {
    const int kind = (selectedGroup < (int)groups.size() && tab == 0) ? groups[(size_t)selectedGroup].graph : kNoGraph;
    if (kind == kNoGraph || selectedGroup >= (int)groups.size()) return;
    std::map<juce::String, int> vals;
    int ms = 0;
    for (const auto& pd : groups[(size_t)selectedGroup].params) {
        const int val = m1::paramGet(pd, block);
        vals[pd.name] = val;
        if (juce::String(pd.name) == "Multisnd") ms = val;
    }
    graphView->setData(kind, vals, ms, 60);
}

void M1Editor::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= (int)groups.size()) return;
    if (selected) g.fillAll(juce::Colour(0xff3d5a80));
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(groups[(size_t)row].title, 8, 0, w - 8, h, juce::Justification::centredLeft);
}

void M1Editor::selectedRowsChanged(int row) {
    if (row >= 0 && row != selectedGroup) { lcdPlay = false; showGroup(row); }
}

void M1Editor::sendParam(const m1::ParamDef& pd, int value) {
    lcdPlay = false;
    lastEdited = pd.name;
    proc.requestParam(currentTarget(), pd, value, (int)kitSlider.getValue() - 1, (int)instSlider.getValue() - 1);
    // Show the new value at once in the local copy; the timer re-reads the real one.
    m1::paramSet(pd, block, value);
}

// Builds the 8 lines of the LCD from the editor's state.
void M1Editor::updateLcd() {
    using Line = LcdView::Line;
    std::array<Line, LcdView::kRows> L;
    auto fit = [](juce::String s, int w) { return s.paddedRight(' ', w).substring(0, w); };
    auto put = [&](int row, int col, const juce::String& s) {
        auto& t = L[(size_t)row].text;
        t = t.paddedRight(' ', col).substring(0, col) + s;
    };
    const bool combi = proc.isCombiMode();
    const auto target = currentTarget();
    const int idx = proc.currentIndex();
    juce::String flags;
    if (proc.isComparing()) flags << "COMPARE ";
    if (proc.isEdited() && tab == 0) flags << "EDITED ";
    if (proc.isProtected()) flags << "PROT";
    auto header = [&](const juce::String& title) {
        L[0].text = fit(title, 40 - flags.length()) + flags;
    };
    auto fmt = [&](const char* name, int off, m1::PK k, int lo, int hi) {
        m1::ParamDef pd{name, (uint8_t)off, k, (int16_t)lo, (int16_t)hi, 0};
        return proc.formatParam(pd, block);
    };

    if (tab == 0 && lcdPlay) {
        if (!combi) {
            header(" PROGRAM  PLAY");
            put(2, 2, "Program " + juce::String(idx).paddedLeft('0', 2));
            put(3, 2, proc.nameAt(false, idx));
            const int mode = block[10];
            put(5, 2, "OSC1 " + fmt("m", 12, m1::PK::Multi, 0, 99));
            if (mode == 1) put(5, 22, "OSC2 " + fmt("m", 14, m1::PK::Multi, 0, 99));
            put(4, 22, mode == 0 ? "SINGLE" : mode == 1 ? "DOUBLE" : "DRUMS");
            put(6, 2, juce::String("FX1 ") + m1::kFxNames[block[m1::kProgFx] < 34 ? block[m1::kProgFx] : 33]);
            put(6, 22, juce::String("FX2 ") + m1::kFxNames[block[m1::kProgFx + 1] < 34 ? block[m1::kProgFx + 1] : 33]);
        } else {
            header(" COMBINATION  PLAY");
            put(2, 2, "Combi " + juce::String(idx).paddedLeft('0', 2));
            put(3, 2, proc.nameAt(true, idx));
            put(3, 22, fmt("t", 10, m1::PK::CombiType, 0, 4));
            for (int t = 0; t < 8; ++t) {
                const int base = 36 + 11 * t;
                const bool off = (block[base + 10] & 0x10) != 0;
                const int prog = juce::jlimit(0, 99, (int)block[base]);
                const juce::String cell = juce::String(t + 1) + ":" + (off ? juce::String("--- off") : juce::String(prog).paddedLeft('0', 2) + " " + proc.nameAt(false, prog));
                put(4 + t % 4, t < 4 ? 1 : 21, fit(cell, 18));
            }
        }
        const juce::String vv = "Vol " + juce::String(proc.getVolumePercent()) + "  Voices " + juce::String(proc.activeVoices());
        if (combi) put(2, 22, vv); else put(7, 2, vv);
    } else {
        const juce::String kind = target == Target::Program ? "PROGRAM" : target == Target::Combi ? "COMBI" : target == Target::Global ? "GLOBAL" : "DRUM KIT";
        const bool haveGroup = selectedGroup < (int)groups.size();
        header(" EDIT " + kind + (haveGroup ? "  " + groups[(size_t)selectedGroup].title : juce::String()));
        if (haveGroup) {
            const auto& g = groups[(size_t)selectedGroup];
            if (g.isName) {
                put(3, 2, "Name = " + proc.nameAt(combi, idx));
            } else {
                int n = 0;
                for (const auto& pd : g.params) {
                    if (n >= 14) break;
                    const int row = 1 + n % 7, col = n < 7 ? 1 : 21;
                    const juce::String cell = fit(pd.name, 10) + " " + fit(proc.formatParam(pd, block), 8);
                    put(row, col, cell);
                    if (lastEdited == pd.name) { L[(size_t)row].invFrom = col; L[(size_t)row].invLen = 19; }
                    ++n;
                }
                if ((int)g.params.size() > 14) put(7, 21, "...");
            }
        }
    }
    for (auto& l : L) l.text = l.text.substring(0, 40);
    lcd->setLines(L);
}

void M1Editor::refreshValues() {
    panel->refresh();
    updateGraph();
}

void M1Editor::timerCallback() {
    if (proc.isCombiMode() != shownCombi || proc.currentIndex() != shownIndex) fillList();
    progButton.setToggleState(!shownCombi, juce::dontSendNotification);
    combiButton.setToggleState(shownCombi, juce::dontSendNotification);
    editTab.setToggleState(tab == 0, juce::dontSendNotification);
    globalTab.setToggleState(tab == 1, juce::dontSendNotification);
    kitTab.setToggleState(tab == 2, juce::dontSendNotification);
    for (auto* b : {&editTab, &globalTab, &kitTab}) b->setClickingTogglesState(false);
    kitSlider.setVisible(tab == 2);
    instSlider.setVisible(tab == 2);
    voicesLabel.setText("Voices: " + juce::String(proc.activeVoices()), juce::dontSendNotification);
    compareButton.setToggleState(proc.isComparing(), juce::dontSendNotification);
    protectButton.setToggleState(proc.isProtected(), juce::dontSendNotification);
    compareButton.setColour(juce::TextButton::buttonColourId, proc.isComparing() ? juce::Colour(0xff8a5a00) : juce::Colour(0xff2a3a3d));
    protectButton.setColour(juce::TextButton::buttonColourId, proc.isProtected() ? juce::Colour(0xff8a1f1f) : juce::Colour(0xff2a3a3d));
    writeButton.setEnabled(!proc.isProtected());
    compareButton.setEnabled(tab == 0);
    editedLabel.setText(proc.isEdited() && tab == 0 ? "edited" : "", juce::dontSendNotification);
    if (!proc.isReady()) statusLabel.setText(proc.statusMessage(), juce::dontSendNotification);
    if (!proc.isReady()) return;

    readBlock();
    const auto sig = structureSignature();
    if (sig != shownSignature) {
        shownSignature = sig;
        const auto prevTitle = selectedGroup < (int)groups.size() ? groups[(size_t)selectedGroup].title : juce::String();
        rebuildGroups();
        int keep = 0;
        for (int i = 0; i < (int)groups.size(); ++i) if (groups[(size_t)i].title == prevTitle) keep = i;
        showGroup(keep);
        resized();
    } else {
        refreshValues();
    }
    updateLcd();
}


void M1Editor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1c1c20));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f));
    g.drawText("Korg M1", 12, 8, 200, 28, juce::Justification::left);
}

void M1Editor::resized() {
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(32);
    auto row = r.removeFromTop(28);
    progButton.setBounds(row.removeFromLeft(70));
    row.removeFromLeft(6);
    combiButton.setBounds(row.removeFromLeft(70));
    row.removeFromLeft(12);
    importButton.setBounds(row.removeFromRight(130));
    row.removeFromRight(12);
    selector.setBounds(row);
    r.removeFromTop(8);
    auto vrow = r.removeFromTop(24);
    volumeLabel.setBounds(vrow.removeFromLeft(60));
    voicesLabel.setBounds(vrow.removeFromRight(100));
    statusLabel.setBounds(vrow.removeFromRight(300));
    volume.setBounds(vrow);
    r.removeFromTop(8);

    auto keys = r.removeFromBottom((int)D110Keyboard::kRefH);
    keyboard.setBounds(keys);
    r.removeFromBottom(8);

    auto lcdRow = r.removeFromTop(LcdView::height());
    if (lcd) lcd->setBounds(lcdRow.withSizeKeepingCentre(LcdView::width(), LcdView::height()));
    r.removeFromTop(8);

    auto tabs = r.removeFromTop(26);
    editTab.setBounds(tabs.removeFromLeft(90));
    tabs.removeFromLeft(4);
    globalTab.setBounds(tabs.removeFromLeft(90));
    tabs.removeFromLeft(4);
    kitTab.setBounds(tabs.removeFromLeft(100));
    tabs.removeFromLeft(12);
    kitSlider.setBounds(tabs.removeFromLeft(150));
    instSlider.setBounds(tabs.removeFromLeft(200));
    r.removeFromTop(6);

    auto wrow = r.removeFromBottom(28);
    r.removeFromBottom(6);
    writeLabel.setBounds(wrow.removeFromLeft(70));
    destBox.setBounds(wrow.removeFromLeft(240));
    wrow.removeFromLeft(8);
    writeButton.setBounds(wrow.removeFromLeft(80));
    wrow.removeFromLeft(8);
    revertButton.setBounds(wrow.removeFromLeft(80));
    wrow.removeFromLeft(8);
    compareButton.setBounds(wrow.removeFromLeft(80));
    wrow.removeFromLeft(8);
    protectButton.setBounds(wrow.removeFromLeft(80));
    wrow.removeFromLeft(12);
    editedLabel.setBounds(wrow.removeFromLeft(80));

    pageList.setBounds(r.removeFromLeft(190));
    r.removeFromLeft(8);
    const bool showGraph = tab == 0 && selectedGroup < (int)groups.size() && groups[(size_t)selectedGroup].graph != kNoGraph;
    if (graphView) {                 // resized() already runs from setSize() in the constructor
        graphView->setVisible(showGraph);
        if (showGraph) graphView->setBounds(r.removeFromTop(170));
    }
    viewport.setBounds(r);
    if (panel) panel->setSize(viewport.getMaximumVisibleWidth(), panel->getHeight());
}
