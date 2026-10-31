cmake_minimum_required(VERSION 3.22)
project(YM303 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")   # universal binary (Apple Silicon + Intel)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.6
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

set(YM_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND YM_FORMATS AU)
endif()

set(YM_SOURCES
  Source/Engine.cpp
  Source/Bank.cpp
  Source/Sequencer.cpp
  Source/PluginProcessor.cpp)

juce_add_plugin(YM303
  COMPANY_NAME               "Yu_My"
  PLUGIN_MANUFACTURER_CODE   YuMy
  PLUGIN_CODE                Y303
  PRODUCT_NAME               "YM303"
  IS_SYNTH                   TRUE
  NEEDS_MIDI_INPUT           TRUE
  NEEDS_MIDI_OUTPUT          FALSE
  IS_MIDI_EFFECT             FALSE
  COPY_PLUGIN_AFTER_BUILD    FALSE
  VST3_CATEGORIES            Instrument Synth
  AU_MAIN_TYPE               kAudioUnitType_MusicDevice
  FORMATS                    ${YM_FORMATS})

target_sources(YM303 PRIVATE ${YM_SOURCES})

target_compile_definitions(YM303 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=1)

target_link_libraries(YM303
  PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices
  PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- optional headless test of the whole plug-in
option(YM_BUILD_TESTS "Build the headless test tools" OFF)
if(YM_BUILD_TESTS)
  juce_add_console_app(YM303_SmokeTest PRODUCT_NAME "YM303 SmokeTest")
  target_sources(YM303_SmokeTest PRIVATE tests/PluginSmoke.cpp ${YM_SOURCES})
  target_compile_definitions(YM303_SmokeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(YM303_SmokeTest PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices
                        juce::juce_recommended_config_flags)
endif()
