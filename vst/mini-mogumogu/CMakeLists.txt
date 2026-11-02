cmake_minimum_required(VERSION 3.22)
project(MiniMogumogu VERSION 1.0.0 LANGUAGES C CXX)

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

set(MM_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND MM_FORMATS AU)
endif()

set(MM_SOURCES
  Source/Engine.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

juce_add_binary_data(MiniMogumogu_Fonts SOURCES
  Fonts/BarlowCondensed-Medium.ttf
  Fonts/BarlowCondensed-SemiBold.ttf
  Fonts/BarlowCondensed-Bold.ttf
  Fonts/ShareTechMono-Regular.ttf)

juce_add_plugin(MiniMogumogu
  COMPANY_NAME               "Yu_My"
  PLUGIN_MANUFACTURER_CODE   YuMy
  PLUGIN_CODE                MMog
  PRODUCT_NAME               "mini mogumogu"
  IS_SYNTH                   TRUE
  NEEDS_MIDI_INPUT           TRUE
  NEEDS_MIDI_OUTPUT          FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  IS_MIDI_EFFECT             FALSE
  COPY_PLUGIN_AFTER_BUILD    FALSE
  VST3_CATEGORIES            Instrument Synth
  AU_MAIN_TYPE               kAudioUnitType_MusicDevice
  FORMATS                    ${MM_FORMATS})

target_sources(MiniMogumogu PRIVATE ${MM_SOURCES})

target_compile_definitions(MiniMogumogu PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=1)

target_link_libraries(MiniMogumogu
  PRIVATE MiniMogumogu_Fonts juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices juce::juce_dsp
  PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- optional headless test tools
option(MM_BUILD_TESTS "Build the headless test tools" OFF)
if(MM_BUILD_TESTS)
  foreach(T SmokeTest GuiShot)
    juce_add_console_app(MiniMogumogu_${T} PRODUCT_NAME "MiniMogumogu ${T}")
    if(T STREQUAL "SmokeTest")
      target_sources(MiniMogumogu_${T} PRIVATE tests/PluginSmoke.cpp ${MM_SOURCES})
    else()
      target_sources(MiniMogumogu_${T} PRIVATE tests/GuiShot.cpp ${MM_SOURCES})
    endif()
    target_compile_definitions(MiniMogumogu_${T} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MiniMogumogu_${T} PRIVATE MiniMogumogu_Fonts juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices juce::juce_dsp
                          juce::juce_recommended_config_flags)
  endforeach()
endif()
