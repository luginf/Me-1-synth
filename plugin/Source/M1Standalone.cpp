// Custom Standalone app for Me-1 (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP).
// Differences from JUCE's stock StandaloneFilterApp:
//   - the window uses the native (system) title bar instead of JUCE's drawn one;
//   - the "Options" button is a bar under the editor, not inside the title bar.
// The menu entries and their actions are the stock ones (StandalonePluginHolder).
#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace {
using namespace juce;

class M1StandaloneWindow final : public DocumentWindow {
public:
    M1StandaloneWindow(const String& title, Colour bg, std::unique_ptr<StandalonePluginHolder> holder)
        : DocumentWindow(title, bg, DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          pluginHolder(std::move(holder)) {
        setUsingNativeTitleBar(true);
        updateContent();
        centreWithSize(getWidth(), getHeight());
        if (auto* props = pluginHolder->settings.get()) {
            const int x = props->getIntValue("windowX", -100), y = props->getIntValue("windowY", -100);
            if (x != -100 && y != -100) {
                const auto area = Desktop::getInstance().getDisplays().getDisplayForRect({x, y, getWidth(), getHeight()});
                if (area != nullptr) {
                    const auto b = area->userBounds.toNearestInt();
                    setTopLeftPosition(jlimit(b.getX(), jmax(b.getX(), b.getRight() - getWidth()), x),
                                       jlimit(b.getY(), jmax(b.getY(), b.getBottom() - getHeight()), y));
                }
            }
        }
        setResizable(false, false);
    }

    ~M1StandaloneWindow() override {
        if (auto* props = pluginHolder->settings.get()) {
            props->setValue("windowX", getX());
            props->setValue("windowY", getY());
        }
        pluginHolder->stopPlaying();
        clearContentComponent();
        pluginHolder = nullptr;
    }

    void closeButtonPressed() override {
        pluginHolder->savePluginState();
        JUCEApplicationBase::quit();
    }

    StandalonePluginHolder& holder() { return *pluginHolder; }

private:
    // Editor on top, a bar with the Options button below it.
    class Content final : public Component, private ComponentListener {
    public:
        static constexpr int kBarHeight = 34;

        explicit Content(M1StandaloneWindow& o) : owner(o) {
            auto* proc = owner.pluginHolder->processor.get();
            editor.reset(proc->hasEditor() ? proc->createEditorAndMakeActive() : new GenericAudioProcessorEditor(*proc));
            addAndMakeVisible(editor.get());
            editor->addComponentListener(this);
            optionsButton.setTriggeredOnMouseDown(true);
            optionsButton.onClick = [this] { showMenu(); };
            addAndMakeVisible(optionsButton);
            setSize(editor->getWidth(), editor->getHeight() + kBarHeight);
        }

        void componentMovedOrResized(Component& c, bool, bool wasResized) override {
            if (wasResized && &c == editor.get() && getHeight() != c.getHeight() + kBarHeight)
                setSize(c.getWidth(), c.getHeight() + kBarHeight);
        }

        ~Content() override {
            editor->removeComponentListener(this);
            owner.pluginHolder->processor->editorBeingDeleted(editor.get());
            editor = nullptr;
        }

        void paint(Graphics& g) override { g.fillAll(Colour(0xff2a2a30)); }

        void resized() override {
            auto r = getLocalBounds();
            auto bar = r.removeFromBottom(kBarHeight);
            editor->setBounds(r);
            optionsButton.setBounds(bar.reduced(6, 4).removeFromLeft(90));
        }

    private:
        void showMenu() {
            PopupMenu m;
            m.addItem(1, TRANS("Audio/MIDI Settings..."));
            m.addSeparator();
            m.addItem(2, TRANS("Save current state..."));
            m.addItem(3, TRANS("Load a saved state..."));
            m.addSeparator();
            m.addItem(4, TRANS("Reset to default state"));
            m.showMenuAsync(PopupMenu::Options().withTargetComponent(optionsButton),
                            [this](int result) { owner.handleMenuResult(result); });
        }

        M1StandaloneWindow& owner;
        std::unique_ptr<AudioProcessorEditor> editor;
        TextButton optionsButton{"Options"};
    };

    void updateContent() {
        setContentOwned(new Content(*this), true);
    }

    void handleMenuResult(int result) {
        switch (result) {
            case 1: pluginHolder->showAudioSettingsDialog(); break;
            case 2: pluginHolder->askUserToSaveState(); break;
            case 3: pluginHolder->askUserToLoadState(); break;
            case 4: {   // deleting the content from inside its own menu callback is deferred
                MessageManager::callAsync([this] { resetToDefaultState(); });
                break;
            }
            default: break;
        }
    }

    void resetToDefaultState() {
        pluginHolder->stopPlaying();
        clearContentComponent();
        pluginHolder->deletePlugin();
        if (auto* props = pluginHolder->settings.get()) props->removeValue("filterState");
        pluginHolder->createPlugin();
        updateContent();
        pluginHolder->startPlaying();
    }

    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};

class M1StandaloneApp final : public JUCEApplication {
public:
    M1StandaloneApp() {
        PropertiesFile::Options options;
        options.applicationName = CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif
        appProperties.setStorageParameters(options);
    }

    const String getApplicationName() override { return CharPointer_UTF8(JucePlugin_Name); }
    const String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const String&) override {}

    void initialise(const String&) override {
        if (Desktop::getInstance().getDisplays().displays.isEmpty()) {
            quit();
            return;
        }
        auto holder = std::make_unique<StandalonePluginHolder>(
            appProperties.getUserSettings(), false, String{}, nullptr, Array<StandalonePluginHolder::PluginInOuts>{}, false);
        window = std::make_unique<M1StandaloneWindow>(
            getApplicationName(), LookAndFeel::getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
            std::move(holder));
        window->setVisible(true);
    }

    void shutdown() override {
        window = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override {
        if (window != nullptr) window->holder().savePluginState();
        if (ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            Timer::callAfterDelay(100, [] {
                if (auto* app = JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
            });
        } else {
            quit();
        }
    }

private:
    ApplicationProperties appProperties;
    std::unique_ptr<M1StandaloneWindow> window;
};
}  // namespace

juce::JUCEApplicationBase* juce_CreateApplication() { return new M1StandaloneApp(); }
