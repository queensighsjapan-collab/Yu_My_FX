cmake_minimum_required(VERSION 3.22)
# must be set BEFORE project() or CMake ignores it (the build would be arm64 only)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")   # universal binary (Apple Silicon + Intel)
endif()

project(YuMyBacteria VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  # static C/C++ runtime: no Visual C++ Redistributable needed on the user's PC
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.6
  GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

set(YB_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND YB_FORMATS AU)
endif()

set(YB_SOURCES
  Source/Engine.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

juce_add_plugin(YuMyBacteria
  COMPANY_NAME               "Yu_My"
  PLUGIN_MANUFACTURER_CODE   YuMy
  PLUGIN_CODE                Ybac
  PRODUCT_NAME               "YuMy Bacteria"
  IS_SYNTH                   TRUE
  NEEDS_MIDI_INPUT           TRUE
  NEEDS_MIDI_OUTPUT          FALSE
  IS_MIDI_EFFECT             FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD    FALSE
  VST3_CATEGORIES            Instrument Synth
  AU_MAIN_TYPE               kAudioUnitType_MusicDevice
  FORMATS                    ${YB_FORMATS})

# logo (embedded into the plug-in binary)
juce_add_binary_data(YuMyBacteriaAssets SOURCES Assets/logo.png)
set_target_properties(YuMyBacteriaAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(YuMyBacteria PRIVATE ${YB_SOURCES})

target_compile_definitions(YuMyBacteria PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=1)

target_compile_options(YuMyBacteria PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-float-equal>)

target_link_libraries(YuMyBacteria
  PRIVATE YuMyBacteriaAssets juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices
  PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- optional headless tests (smoke test of the whole plug-in)
option(YB_BUILD_TESTS "Build the headless test tools" OFF)
if(YB_BUILD_TESTS)
  foreach(T SmokeTest GuiShot)
    if(T STREQUAL "SmokeTest")
      set(SRC tests/PluginSmoke.cpp)
    else()
      set(SRC tests/GuiShot.cpp)
    endif()
    juce_add_console_app(YuMyBacteria_${T} PRODUCT_NAME "YuMy Bacteria ${T}")
    target_sources(YuMyBacteria_${T} PRIVATE ${SRC} ${YB_SOURCES})
    target_compile_definitions(YuMyBacteria_${T} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(YuMyBacteria_${T} PRIVATE YuMyBacteriaAssets juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_devices
                          juce::juce_recommended_config_flags)
  endforeach()
endif()
