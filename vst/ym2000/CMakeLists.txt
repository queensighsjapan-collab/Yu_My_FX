cmake_minimum_required(VERSION 3.22)
project(YM2000 VERSION 1.0.0 LANGUAGES C CXX)

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
  Source/Fx.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

juce_add_plugin(YM2000
  COMPANY_NAME               "Yu_My"
  PLUGIN_MANUFACTURER_CODE   YuMy
  PLUGIN_CODE                Ym20
  PRODUCT_NAME               "YM2000"
  IS_SYNTH                   TRUE
  NEEDS_MIDI_INPUT           TRUE
  NEEDS_MIDI_OUTPUT          FALSE
  IS_MIDI_EFFECT             FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD    FALSE
  VST3_CATEGORIES            Instrument Synth
  AU_MAIN_TYPE               kAudioUnitType_MusicDevice
  FORMATS                    ${YM_FORMATS})

target_sources(YM2000 PRIVATE ${YM_SOURCES})

target_compile_definitions(YM2000 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=1)

target_compile_options(YM2000 PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-float-equal>)

target_link_libraries(YM2000
  PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices
  PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- optional headless tests (smoke test of the whole plug-in, GUI screenshot)
option(YM_BUILD_TESTS "Build the headless test tools" OFF)
if(YM_BUILD_TESTS)
  foreach(T SmokeTest GuiShot)
    if(T STREQUAL "SmokeTest")
      set(SRC tests/PluginSmoke.cpp)
    else()
      set(SRC tests/GuiShot.cpp)
    endif()
    juce_add_console_app(YM2000_${T} PRODUCT_NAME "YM2000 ${T}")
    target_sources(YM2000_${T} PRIVATE ${SRC} ${YM_SOURCES})
    target_compile_definitions(YM2000_${T} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(YM2000_${T} PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices
                          juce::juce_recommended_config_flags)
  endforeach()
endif()
