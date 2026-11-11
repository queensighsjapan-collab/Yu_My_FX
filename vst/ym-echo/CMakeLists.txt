cmake_minimum_required(VERSION 3.22)

project(YMEcho VERSION 0.1.0 LANGUAGES C CXX)

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
# YM Echo - a small stereo delay VST3 effect.
# Its job is to be a known-good plugin for testing YM ORPHEUS's M2
# VST3 hosting (scan -> load -> hear it work). Original code.
# ---------------------------------------------------------------------------
juce_add_plugin(YMEcho
    PRODUCT_NAME                 "YM Echo"
    VERSION                      "0.1.0"
    COMPANY_NAME                 "YM"
    BUNDLE_ID                    "jp.ym.ymecho"
    DESCRIPTION                  "Simple stereo delay (test effect)"
    PLUGIN_MANUFACTURER_CODE     "YMmg"
    PLUGIN_CODE                  "Ymec"
    FORMATS                      VST3
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              "Fx|Delay"
)

juce_generate_juce_header(YMEcho)

target_sources(YMEcho PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(YMEcho PRIVATE
    Source
)

target_compile_definitions(YMEcho PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(YMEcho PRIVATE
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
