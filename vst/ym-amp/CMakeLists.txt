cmake_minimum_required(VERSION 3.22)

project(YMAmp VERSION 1.1.0 LANGUAGES C CXX)

# NeuralAmpModelerCore (vendored under ThirdParty/) requires C++20
# (std::span etc.), unlike the other YM plugins which use C++17.
set(CMAKE_CXX_STANDARD 20)
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
# NeuralAmpModelerCore (MIT) - vendored snapshot, pinned at upstream
# commit 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842. Vendored (rather than
# fetched) so the build also works offline and never depends on the
# Eigen submodule host. See ThirdParty/NeuralAmpModelerCore/LICENSE.
# The A2 fast-path WaveNet is enabled, matching upstream's default.
# ---------------------------------------------------------------------------
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/NeuralAmpModelerCore")

file(GLOB NAM_SOURCES
    "${NAM_CORE_DIR}/NAM/*.cpp"
    "${NAM_CORE_DIR}/NAM/*/*.cpp"
)

# The NAM core registers its architecture parsers through static
# initialiser objects in each translation unit (e.g.
# _register_SlimmableContainer in container.cpp). If the sources were
# compiled into the plugin's static shared-code library, the linker
# would drop those "unreferenced" units and get_dsp() would fail with
# "No config parser registered for architecture: ...". Building them
# as an OBJECT library and injecting the objects directly into each
# final artefact (VST3 / Standalone / smoke) prevents that.
add_library(YMAmpNAM OBJECT ${NAM_SOURCES})

target_include_directories(YMAmpNAM PRIVATE
    ${NAM_CORE_DIR}
    ${NAM_CORE_DIR}/NAM
)

target_include_directories(YMAmpNAM SYSTEM PRIVATE
    ${NAM_CORE_DIR}/Dependencies/eigen
    ${NAM_CORE_DIR}/Dependencies/nlohmann
)

target_compile_definitions(YMAmpNAM PRIVATE
    NAM_ENABLE_A2_FAST=1
    NAM_SAMPLE_FLOAT=1
)

set_target_properties(YMAmpNAM PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Embedded factory content: two CC BY .nam captures (see CREDITS.txt)
# and one cab IR synthesised in-house (no third-party IR is bundled).
# ---------------------------------------------------------------------------
juce_add_binary_data(YMAmpData
    SOURCES
        Resources/EVH_5150_III_AmpOnly.nam
        Resources/KT66_Plexi_1968.nam
        Resources/YMCab_4x12.wav
)

# ---------------------------------------------------------------------------
# YM Amp - a Neural Amp Modeler (NAM) player. Loads .nam captures
# (A1 and A2, including SlimmableContainer) via NeuralAmpModelerCore,
# resamples around the NAM stage when the host is not at the model's
# 48 kHz, and runs a full amp chain: input source (live / built-in
# stem file player) -> input gain -> noise gate -> treble BOOST ->
# NAM model -> cab IR convolution -> 3-band EQ + Presence -> CHORUS
# -> DELAY -> REVERB -> MASTER. Ships as VST3 and as a Standalone app
# (same processor/editor) so it can also run without a DAW, using the
# audio interface directly through the standalone wrapper's device
# settings. Original code only, apart from the vendored NAM core.
# ---------------------------------------------------------------------------
juce_add_plugin(YMAmp
    PRODUCT_NAME                 "YM Amp"
    VERSION                      "1.1.0"
    COMPANY_NAME                 "YM"
    BUNDLE_ID                    "jp.ym.ymamp"
    DESCRIPTION                  "Neural amp modeler (NAM) player with built-in FX and stem player"
    PLUGIN_MANUFACTURER_CODE     "YMmg"
    PLUGIN_CODE                  "Yamp"
    FORMATS                      VST3 Standalone
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             TRUE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              "Fx"
)

juce_generate_juce_header(YMAmp)

target_sources(YMAmp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(YMAmp PRIVATE
    Source
    ${NAM_CORE_DIR}
    ${NAM_CORE_DIR}/NAM
)

target_include_directories(YMAmp SYSTEM PRIVATE
    ${NAM_CORE_DIR}/Dependencies/eigen
    ${NAM_CORE_DIR}/Dependencies/nlohmann
)

target_compile_definitions(YMAmp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    NAM_ENABLE_A2_FAST=1
    NAM_SAMPLE_FLOAT=1
)

target_link_libraries(YMAmp PRIVATE
    YMAmpData
    juce::juce_audio_basics
    juce::juce_audio_formats
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

# Inject the NAM core objects into each final artefact (see the
# YMAmpNAM note above).
foreach(ymampFormatTarget YMAmp_VST3 YMAmp_Standalone)
    if(TARGET ${ymampFormatTarget})
        target_sources(${ymampFormatTarget} PRIVATE $<TARGET_OBJECTS:YMAmpNAM>)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Headless smoke harness (off by default; NOT part of the deliverable
# build). Links the plugin's shared-code target and exercises the DSP
# without a host:  cmake -B build -DYMAMP_SMOKE=ON
# ---------------------------------------------------------------------------
option(YMAMP_SMOKE "Build the headless smoke-test harness" OFF)

if(YMAMP_SMOKE)
    add_executable(YMAmpSmoke Tests/SmokeMain.cpp)
    # The generated JuceHeader.h / JucePluginDefines.h live in the
    # plugin target's JuceLibraryCode dir and are not propagated
    # through the shared-code target, so add them explicitly.
    target_include_directories(YMAmpSmoke PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/YMAmp_artefacts/JuceLibraryCode")
    add_executable(YMAmpConvProbe Tests/ConvProbe.cpp)
    target_compile_definitions(YMAmpConvProbe PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)
    target_include_directories(YMAmpConvProbe PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/YMAmp_artefacts/JuceLibraryCode")
    target_link_libraries(YMAmpConvProbe PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics)

    target_sources(YMAmpSmoke PRIVATE $<TARGET_OBJECTS:YMAmpNAM>)
    target_link_libraries(YMAmpSmoke PRIVATE YMAmp YMAmpData
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra)
endif()
