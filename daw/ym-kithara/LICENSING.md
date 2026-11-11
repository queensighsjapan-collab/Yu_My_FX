# Licensing — YM-Kithara

YM-Kithara is developed from the start as an **open-source** project.

## YM-Kithara itself

YM-Kithara will be released under a **GPL-family licence**. The final choice
between **GPLv3** and **AGPLv3** is still pending and will be recorded here
(and in the `LICENSE` file) before the first public release.

Releasing the source is not optional for this project: it is a condition of
the JUCE licence terms under which the framework is being used, and the
project embraces it — all DAW code in this repository is original work,
written for YM-Kithara.

## Third-party components

| Component | Licence / terms | Notes |
|---|---|---|
| **JUCE** (8.0.15, fetched by CMake) | Used under JUCE's open-source (AGPLv3) terms, via the free licence held by the project owner | Framework: audio devices, audio formats, GUI. Fetched at configure time from the official JUCE repository; not vendored in this repo. |
| **FFmpeg** (libavformat / libavcodec / libavutil / libswscale) | **GPL** build (deliberate — matches this project's GPL release) | Video decoding for the video track (M6). On Windows, BtbN FFmpeg-Builds' GPL shared build is downloaded by build.bat into `deps/ffmpeg` (not vendored); its DLLs ship next to the exe. On Linux, system dev packages via pkg-config. See [THIRD_PARTY.md](THIRD_PARTY.md). |
| **VST3 SDK** (Steinberg) | **MIT** (since SDK 3.8) | Will be used from milestone M2 onwards for VST3 plug-in hosting. Not yet included in M0. |
| **ASIO SDK** (Steinberg) | Dual licence — used under its **GPL arm** when ASIO support is enabled | Optional at build time (`-DASIO_SDK_DIR=...`). The SDK itself is **not** distributed with this repository; each builder downloads it from Steinberg. |
| **Ardour / LMMS** (reference projects) | GPL (their own terms) | Studied for architecture and design lessons only. **No code, headers, or assets from these projects are used in YM-Kithara.** |

## A note on plug-ins

VST3 plug-ins hosted by YM-Kithara remain the property of their respective
authors and are loaded at runtime by the user; no plug-in binaries are
distributed with YM-Kithara.
