cmake_minimum_required(VERSION 3.22)

project(MeronTronIII VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE (fetched at configure time; pinned to an exact tag, same as the
# other YM plugins)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# MeronTronIII - tape-replay instrument rebuilt from the 2013 SynthEdit
# VST2 MeronTronII. The SoundFont was recovered sample-by-sample from the
# original DLL (ReactOS renders + solved pdta metadata); the two dead
# notes (Flute key 70, Strings key 86) are fixed in the font itself.
# Playback: polyphonic one-shot sample engine with VOLUME / TONE /
# ATTACK / RELEASE / REVERB and a HALF SPEED tape switch.
# Editor: functional first version in the spirit of the approved mock
# (cream enamel + walnut + brass, 1120x660). No mock-only markers.
# ---------------------------------------------------------------------------
juce_add_plugin(MeronTronIII
    PRODUCT_NAME                 "MeronTronIII"
    VERSION                      "1.0.0"
    COMPANY_NAME                 "YM"
    BUNDLE_ID                    "jp.ym.merontroniii"
    DESCRIPTION                  "Tape replay instrument (Mellotron-style SF2 player)"
    PLUGIN_MANUFACTURER_CODE     "YMmg"
    PLUGIN_CODE                  "Mrn3"
    FORMATS                      VST3 Standalone
    IS_SYNTH                     TRUE
    NEEDS_MIDI_INPUT             TRUE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  TRUE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              "Instrument"
)

juce_generate_juce_header(MeronTronIII)

target_sources(MeronTronIII PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Sf2Font.cpp
)

target_include_directories(MeronTronIII PRIVATE
    Source
)

target_compile_definitions(MeronTronIII PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(MeronTronIII PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
)

# ---------------------------------------------------------------------------
# The restored SoundFont ships next to the binaries: inside the VST3
# bundle (Contents/Resources) and beside the standalone executable. The
# processor searches both places at runtime (see findFontFile()).
# ---------------------------------------------------------------------------
if(TARGET MeronTronIII_VST3)
    add_custom_command(TARGET MeronTronIII_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:MeronTronIII_VST3>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/MeronTronIII.sf2"
            "$<TARGET_FILE_DIR:MeronTronIII_VST3>/../Resources/MeronTronIII.sf2")
endif()

if(TARGET MeronTronIII_Standalone)
    add_custom_command(TARGET MeronTronIII_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/MeronTronIII.sf2"
            "$<TARGET_FILE_DIR:MeronTronIII_Standalone>/MeronTronIII.sf2")
endif()

# ---------------------------------------------------------------------------
# Headless smoke harness (off by default; NOT part of the deliverable
# build). Renders notes through the real processor and checks the SF2
# load, dead-note keys, release behaviour, HALF SPEED pitch and program
# change:  cmake -B build -DMERONTRONIII_SMOKE=ON
# ---------------------------------------------------------------------------
option(MERONTRONIII_SMOKE "Build the headless smoke-test harness" OFF)

if(MERONTRONIII_SMOKE)
    add_executable(MeronTronIIISmoke Tests/SmokeMain.cpp)
    target_include_directories(MeronTronIIISmoke PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/MeronTronIII_artefacts/JuceLibraryCode")
    target_link_libraries(MeronTronIIISmoke PRIVATE MeronTronIII
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)
endif()
