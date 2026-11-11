cmake_minimum_required(VERSION 3.22)

project(YMKithara VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE (fetched at configure time; pinned to an exact tag)
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
# Optional ASIO support (Windows)
#
# Pass -DASIO_SDK_DIR=<path to the Steinberg ASIO SDK> at configure time.
# When it is not given, the app still builds and runs with WASAPI (and, on
# other platforms, the native backends JUCE provides).
# ---------------------------------------------------------------------------
set(ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK root folder")

set(ASIO_ENABLED OFF)
if(WIN32 AND ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}")
    set(ASIO_ENABLED ON)
    message(STATUS "YM-Kithara: ASIO support enabled (SDK: ${ASIO_SDK_DIR})")
elseif(WIN32 AND ASIO_SDK_DIR)
    message(WARNING "YM-Kithara: ASIO_SDK_DIR was given but does not exist: ${ASIO_SDK_DIR}")
endif()

# ---------------------------------------------------------------------------
# FFmpeg (M6 video decoding + M6.2 mp4 export: libavformat / libavcodec /
# libavutil / libswscale / libswresample — GPL build, matching
# YM-Kithara's own GPL release terms; see THIRD_PARTY.md).
#
# Two ways in:
#   1. FFMPEG_ROOT — a folder holding include/ and lib/. On Windows,
#      build.bat downloads BtbN's shared build into deps/ffmpeg and
#      passes this automatically (the folder is picked up from there
#      even without the explicit -D).
#   2. pkg-config (Linux/macOS system packages: libavformat-dev etc.).
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg root folder (contains include/ and lib/)")

if(NOT FFMPEG_ROOT AND WIN32
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/ffmpeg/include/libavformat/avformat.h")
    set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/ffmpeg")
endif()

set(FFMPEG_INCLUDE_DIRS "")
set(FFMPEG_LINK_LIBRARIES "")
set(FFMPEG_RUNTIME_DLLS "")

if(FFMPEG_ROOT)
    if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
        message(FATAL_ERROR
            "YM-Kithara: FFMPEG_ROOT was given but has no include/libavformat/avformat.h: ${FFMPEG_ROOT}")
    endif()

    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")

    foreach(ffmpegLib avformat avcodec avutil swscale swresample)
        if(WIN32)
            set(ffmpegLibFile "${FFMPEG_ROOT}/lib/${ffmpegLib}.lib")
        else()
            set(ffmpegLibFile "${FFMPEG_ROOT}/lib/lib${ffmpegLib}.so")
        endif()

        if(NOT EXISTS "${ffmpegLibFile}")
            message(FATAL_ERROR "YM-Kithara: missing FFmpeg library: ${ffmpegLibFile}")
        endif()

        list(APPEND FFMPEG_LINK_LIBRARIES "${ffmpegLibFile}")
    endforeach()

    if(WIN32)
        # The DLLs sit next to the .exe (build.bat's flow and manual
        # builds alike); swresample comes along when present.
        file(GLOB FFMPEG_RUNTIME_DLLS
             "${FFMPEG_ROOT}/bin/avformat-*.dll"
             "${FFMPEG_ROOT}/bin/avcodec-*.dll"
             "${FFMPEG_ROOT}/bin/avutil-*.dll"
             "${FFMPEG_ROOT}/bin/swscale-*.dll"
             "${FFMPEG_ROOT}/bin/swresample-*.dll")
    endif()

    message(STATUS "YM-Kithara: FFmpeg from FFMPEG_ROOT: ${FFMPEG_ROOT}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
                      libavformat libavcodec libavutil libswscale libswresample)
    set(FFMPEG_LINK_LIBRARIES PkgConfig::FFMPEG)
    message(STATUS "YM-Kithara: FFmpeg from pkg-config (system packages)")
endif()

# ---------------------------------------------------------------------------
# Application target
# ---------------------------------------------------------------------------
juce_add_gui_app(YMKithara
    PRODUCT_NAME            "YM-Kithara"
    VERSION                 "0.1.0"
    COMPANY_NAME            "YM"
    BUNDLE_ID               "net.ym.orpheus"
    MICROPHONE_PERMISSION_ENABLED FALSE
    CAMERA_PERMISSION_ENABLED     FALSE
    # App icon (M6): a golden lyre over waveform strings on navy,
    # chosen by かいさん — JUCE generates the Windows .ico/exe icon
    # from ICON_BIG. A pre-made multi-size .ico sits alongside in
    # Resources/ for any tooling that wants one directly.
    ICON_BIG                Resources/orpheus-icon.png
    ICON_SMALL              Resources/orpheus-icon.png
)

juce_generate_juce_header(YMKithara)

target_sources(YMKithara PRIVATE
    Source/Main.cpp
    Source/Engine/TimelineEngine.cpp
    Source/Engine/PluginManager.cpp
    Source/Engine/VideoDecoder.cpp
    Source/Engine/Mp4Exporter.cpp
    Source/Engine/VideoComposite.cpp
    Source/Engine/SrtParser.cpp
    Source/Engine/VideoAudioExtractor.cpp
    Source/UI/MainComponent.cpp
    Source/UI/TimelineComponent.cpp
    Source/UI/TrackHeaderComponent.cpp
    Source/UI/TelopStyleDialog.cpp
    Source/UI/PianoRollComponent.cpp
    Source/UI/VideoPreviewComponent.cpp
)

target_include_directories(YMKithara PRIVATE
    Source
    Source/Engine
    Source/UI
    ${FFMPEG_INCLUDE_DIRS}
)

target_compile_definitions(YMKithara PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(YMKithara PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    ${FFMPEG_LINK_LIBRARIES}
)

# Copy the FFmpeg runtime DLLs next to the app (Windows + FFMPEG_ROOT
# only; on pkg-config systems the loader finds the system libraries).
foreach(ffmpegDll ${FFMPEG_RUNTIME_DLLS})
    add_custom_command(TARGET YMKithara POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ffmpegDll}"
                $<TARGET_FILE_DIR:YMKithara>
        COMMENT "Copying FFmpeg runtime DLL")
endforeach()

# ---------------------------------------------------------------------------
# VST3 scanner (M2): a separate console program that enumerates the
# machine's VST3 plugins OUT OF PROCESS (a crashing plugin can only kill
# the scanner, never the DAW) and writes the result as a KnownPluginList
# XML. The main app runs it via "Scan for Plugins..." and copies of the
# binary are placed next to the main app executable below.
# ---------------------------------------------------------------------------
juce_add_console_app(KitharaVstScanner
    PRODUCT_NAME "KitharaVstScanner"
    VERSION      "0.1.0"
    COMPANY_NAME "YM"
    BUNDLE_ID    "net.ym.orpheus.vstscanner"
)

juce_generate_juce_header(KitharaVstScanner)

target_sources(KitharaVstScanner PRIVATE
    Source/Scanner/ScannerMain.cpp
)

target_include_directories(KitharaVstScanner PRIVATE
    Source
)

target_compile_definitions(KitharaVstScanner PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_link_libraries(KitharaVstScanner PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
)

# Keep the scanner next to the app executable so the app always finds it.
add_dependencies(YMKithara KitharaVstScanner)

add_custom_command(TARGET YMKithara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:KitharaVstScanner>
            $<TARGET_FILE_DIR:YMKithara>
    COMMENT "Copying KitharaVstScanner next to YM-Kithara"
)

if(ASIO_ENABLED)
    target_compile_definitions(YMKithara PRIVATE JUCE_ASIO=1)
    target_include_directories(YMKithara PRIVATE
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
        "${ASIO_SDK_DIR}/host/pc"
    )
endif()
