cmake_minimum_required(VERSION 3.22)

project(YMMirage VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE (fetched at configure time; pinned to an exact tag, same as ORPHEUS)
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
# YM Mirage - a granular cloud VST3 effect. The input is written into a
# 4-second circular buffer; a scheduler spawns short Hann-windowed grains
# that re-read the buffer's mid signal at a pitch-shifted rate, at random
# pans and smeared read positions, and are summed into a cloud. FREEZE
# stops the write head so the cloud sustains a captured moment forever.
# Original code only.
# ---------------------------------------------------------------------------
juce_add_plugin(YMMirage
    PRODUCT_NAME                 "YM Mirage"
    VERSION                      "1.1.0"
    COMPANY_NAME                 "YM"
    BUNDLE_ID                    "jp.ym.ymmirage"
    DESCRIPTION                  "Granular cloud effect with freeze"
    PLUGIN_MANUFACTURER_CODE     "YMmg"
    PLUGIN_CODE                  "Ymir"
    FORMATS                      VST3
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             TRUE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              "Fx"
)

juce_generate_juce_header(YMMirage)

target_sources(YMMirage PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(YMMirage PRIVATE
    Source
)

target_compile_definitions(YMMirage PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(YMMirage PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
)
