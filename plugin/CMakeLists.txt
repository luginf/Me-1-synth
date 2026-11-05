cmake_minimum_required(VERSION 3.22)
project(Me-1 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: an existing checkout, not vendored here. Override with -DJUCE_DIR=...
set(JUCE_DIR "/home/alan/src/juce-checkouts/JUCE-8.0.13" CACHE PATH "JUCE source tree")
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

# ---- ROM conversion (configure time) ---------------------------------------
# The converter writes m1_rom_tables.h and m1_pcm.bin into the build tree. The
# header is compiled into the binary: a build made from your ROMs is for you
# only and must not be redistributed (README.md).
set(M1_ROM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../roms" CACHE PATH "Directory holding the M1 ROM set")
set(_m1_gen "${CMAKE_BINARY_DIR}/m1_rom")
set(_m1_make_blob "${CMAKE_CURRENT_SOURCE_DIR}/../tools/m1_make_blob.py")
file(GLOB _m1_syx "${M1_ROM_DIR}/*.syx" "${M1_ROM_DIR}/*.SYX")
set(_m1_have_roms TRUE)
foreach(_n IC36 IC37 IC38 IC39 IC40 IC41 IC42 IC43 IC30)
    if(NOT EXISTS "${M1_ROM_DIR}/${_n}.bin")
        set(_m1_have_roms FALSE)
    endif()
endforeach()
if(NOT _m1_syx)
    set(_m1_have_roms FALSE)
endif()
if(NOT _m1_have_roms)
    message(STATUS "Me-1: skipped - no ROM set in ${M1_ROM_DIR} (see roms/README.md)")
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB _m1_scripts "${CMAKE_CURRENT_SOURCE_DIR}/../tools/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_m1_scripts} ${_m1_syx})
foreach(_f ${_m1_scripts} ${_m1_syx})
    if(EXISTS "${_m1_gen}/m1_rom_tables.h" AND "${_f}" IS_NEWER_THAN "${_m1_gen}/m1_rom_tables.h")
        file(REMOVE "${_m1_gen}/m1_rom_tables.h")
    endif()
endforeach()
if(NOT EXISTS "${_m1_gen}/m1_rom_tables.h")
    message(STATUS "Me-1: converting the ROM set (takes a moment)")
    execute_process(COMMAND ${Python3_EXECUTABLE} -I "${_m1_make_blob}" "${M1_ROM_DIR}" "${_m1_gen}"
                    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "Me-1: ROM conversion failed\n${_out}${_err}")
    endif()
    message(STATUS "Me-1: ${_out}")
endif()

set(_eng "${CMAKE_CURRENT_SOURCE_DIR}/../m1_engine")
set(M1_COMMON_SOURCES
    Source/M1AudioProcessor.cpp
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
    M1_PCM_DEV_PATH="${_m1_gen}/m1_pcm.bin")

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
target_include_directories(Me-1 PRIVATE ${_eng}/include "${_m1_gen}")
target_compile_definitions(Me-1 PUBLIC ${M1_DEFS} JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_link_libraries(Me-1 PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Standalone: put the PCM beside the binary, the path a real user would use.
if(TARGET Me-1_Standalone)
    # Own app: native title bar, Options button under the editor (Source/M1Standalone.cpp).
    target_sources(Me-1_Standalone PRIVATE Source/M1Standalone.cpp)
    add_custom_command(TARGET Me-1_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_m1_gen}/m1_pcm.bin"
                "$<TARGET_FILE_DIR:Me-1_Standalone>/m1-roms/m1_pcm.bin")
endif()

# VST3: the plugin's "executable directory" is the .so inside the bundle, so the
# PCM goes in m1-roms/ right there.
if(TARGET Me-1_VST3)
    add_custom_command(TARGET Me-1_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_m1_gen}/m1_pcm.bin"
                "$<TARGET_FILE_DIR:Me-1_VST3>/m1-roms/m1_pcm.bin")
endif()

# Headless probes (processor + two-instance check).
juce_add_console_app(M1Probe PRODUCT_NAME M1Probe)
target_sources(M1Probe PRIVATE m1_probe.cpp ${M1_COMMON_SOURCES})
target_include_directories(M1Probe PRIVATE ${_eng}/include "${_m1_gen}" Source)
target_compile_definitions(M1Probe PUBLIC ${M1_DEFS} JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(M1Probe PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags)
