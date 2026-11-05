#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

#include "D110Keyboard.h"
#include "M1AudioProcessor.h"

// Parameter editor for the M1, generated from the engine's parameter tables
// (m1_params.h: the same PageDef/ParamDef the firmware uses, read here
// directly, without the firmware's two-encoder page controller). Edit pages
// follow Program/Combination; Global and Drum Kits have their own tabs.
class M1Editor : public juce::AudioProcessorEditor,
                 private juce::Timer,
                 private juce::ListBoxModel {
public:
    explicit M1Editor(M1AudioProcessor&);
    ~M1Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Target = M1AudioProcessor::EditTarget;

    struct Group {
        juce::String title;
        std::vector<m1::ParamDef> params;   // offsets already resolved (timbre, effect base)
        bool isName = false;
        int graph = 0;                      // GraphKind, from the page title
    };
    class ParamRow;
    class ParamPanel;
    class GraphView;
    class LcdView;

    void timerCallback() override;
    int getNumRows() override { return (int)groups.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int row) override;

    void fillList();
    Target currentTarget() const;
    void readBlock();
    juce::String structureSignature() const;
    void rebuildGroups();
    void showGroup(int index);
    void refreshValues();
    void updateGraph();
    void updateLcd();
    void sendParam(const m1::ParamDef&, int value);

    M1AudioProcessor& proc;
    juce::TextButton progButton{"PROG"}, combiButton{"COMBI"}, importButton{"Import SysEx..."};
    juce::TextButton editTab{"Edit"}, globalTab{"Global"}, kitTab{"Drum Kits"};
    juce::ComboBox selector, destBox;
    juce::TextButton writeButton{"Write"}, revertButton{"Revert"}, compareButton{"Compare"}, protectButton{"Protect"};
    std::unique_ptr<GraphView> graphView;
    std::unique_ptr<LcdView> lcd;
    bool lcdPlay = true;            // play screen vs the current edit page
    juce::String lastEdited;        // name of the parameter touched last (highlighted on the LCD)
    juce::Slider volume, kitSlider, instSlider;
    juce::Label volumeLabel{{}, "Volume"}, statusLabel, voicesLabel, editedLabel, writeLabel{{}, "Write to"};
    juce::ListBox pageList{{}, this};
    std::unique_ptr<ParamPanel> panel;
    juce::Viewport viewport;
    D110Keyboard keyboard;       // the on-screen keyboard of the JiV-881 and Di-111 projects
    std::unique_ptr<juce::FileChooser> chooser;

    int tab = 0;                    // 0 edit, 1 global, 2 kits
    bool shownCombi = false;
    int shownIndex = -1;
    int shownRev = -1;
    juce::String shownSignature;
    int destFilledFor = -1;
    std::vector<Group> groups;
    int selectedGroup = 0;
    uint8_t block[210]{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(M1Editor)
};
