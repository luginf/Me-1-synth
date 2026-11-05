cmake_minimum_required(VERSION 3.22)
project(Me-1 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: an existing checkout when JUCE_DIR points at one (local builds), otherwise
# fetched at the version the CI uses. Not vendored here.
set(JUCE_DIR "/home/alan/src/juce-checkouts/JUCE-8.0.13" CACHE PATH "JUCE source tree")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- ROMs -------------------------------------------------------------------
# No ROM data is converted, embedded or copied at build time: the plugin reads
# the user's own ROM files at runtime (Source/M1RomLoader.cpp). For development
# builds, M1_DEV_ROM_DIR (a path only) is added to the plugin's search list.
set(M1_DEV_ROM_DIR "" CACHE PATH "Optional ROM folder for development builds (path only, nothing is copied)")

set(_eng "${CMAKE_CURRENT_SOURCE_DIR}/../m1_engine")
set(M1_COMMON_SOURCES
    Source/M1AudioProcessor.cpp
    Source/M1RomLoader.cpp
    Source/M1Editor.cpp
    Source/D110Keyboard.cpp
    Source/UiTheme.cpp
    ${_eng}/src/m1_engine/m1_engine.cpp
    ${_eng}/src/m1_engine/m1_fx.cpp
    ${_eng}/src/M1_SysEx.cpp
    ${_eng}/src/m1_params.cpp
    ${_eng}/src/m1_memory.cpp)

set(M1_DEFS
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
)
if(M1_DEV_ROM_DIR)
    list(APPEND M1_DEFS M1_DEV_ROM_DIR="${M1_DEV_ROM_DIR}")
endif()

juce_add_plugin(Me-1
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "DIY"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Mk01
    FORMATS VST3 Standalone
    PRODUCT_NAME "Me-1")
target_sources(Me-1 PRIVATE ${M1_COMMON_SOURCES})
target_include_directories(Me-1 PRIVATE ${_eng}/include Source)
target_compile_definitions(Me-1 PUBLIC ${M1_DEFS} JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_link_libraries(Me-1 PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Standalone: own app with a native title bar and an Options button under the editor.
if(TARGET Me-1_Standalone)
    target_sources(Me-1_Standalone PRIVATE Source/M1Standalone.cpp)
endif()

# Headless probes (processor + two-instance check).
juce_add_console_app(M1Probe PRODUCT_NAME M1Probe)
target_sources(M1Probe PRIVATE m1_probe.cpp ${M1_COMMON_SOURCES})
target_include_directories(M1Probe PRIVATE ${_eng}/include Source)
target_compile_definitions(M1Probe PUBLIC ${M1_DEFS} JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(M1Probe PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags)
