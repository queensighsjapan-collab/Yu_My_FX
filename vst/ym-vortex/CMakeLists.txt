cmake_minimum_required(VERSION 3.22)

project(YMVortex VERSION 1.1.0 LANGUAGES C CXX)

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
# YM Vortex - a multi-stage phaser VST3 effect. A cascade of first-order
# allpass stages (4 / 6 / 8 / 12, log-spaced centres) is swept by two
# LFOs at incommensurate rates, so the notch pattern keeps evolving
# instead of repeating. Per-channel chains with a phase-offset stereo
# width, signed feedback with a soft-clipped return, and the YM house
# standard MIDI-CC bypass. Original code only.
# ---------------------------------------------------------------------------
juce_add_plugin(YMVortex
    PRODUCT_NAME                 "YM Vortex"
    VERSION                      "1.1.0"
    COMPANY_NAME                 "YM"
    BUNDLE_ID                    "jp.ym.ymvortex"
    DESCRIPTION                  "Multi-stage phaser with dual-LFO sweep"
    PLUGIN_MANUFACTURER_CODE     "YMmg"
    PLUGIN_CODE                  "Yvtx"
    FORMATS                      VST3
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             TRUE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              "Fx"
)

juce_generate_juce_header(YMVortex)

target_sources(YMVortex PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(YMVortex PRIVATE
    Source
)

target_compile_definitions(YMVortex PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(YMVortex PRIVATE
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
