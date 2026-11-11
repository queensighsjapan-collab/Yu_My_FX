# Third-party components

YM-Kithara itself is released under the GPL (see [LICENSING.md](LICENSING.md)).
This file records the third-party components it builds and runs against.

## FFmpeg (libavformat / libavcodec / libavutil / libswscale)

- **Used for:** video file decoding for the video track (M6): probing
  stream info, seeking, frame decoding and scaling to RGB — and, since
  M6.2, mp4 export: H.264 encoding via **libx264** (a GPL component,
  present in the GPL builds above), AAC encoding with FFmpeg's native
  encoder, audio resampling via libswresample, scaling via
  libswscale, and mp4 muxing with faststart via libavformat.
- **Licence:** the build used here is the **GPL** build of FFmpeg
  (FFmpeg itself is LGPL 2.1+ at its core, with optional GPL parts;
  the shared builds below are configured with `--enable-gpl`, so the
  GPL applies to the combined libraries). Using the GPL build is a
  deliberate choice: YM-Kithara is GPL open source, so the terms are
  compatible. Source code for FFmpeg is available from
  https://ffmpeg.org/ and from the build projects below.
- **Windows:** [BtbN FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)
  `ffmpeg-master-latest-win64-gpl-shared.zip`, downloaded automatically
  by `build.bat` on first build into `deps/ffmpeg` (not part of this
  repository or of the source zip). The runtime DLLs (avformat,
  avcodec, avutil, swscale, swresample) are copied next to
  `YM-Kithara.exe` at build time and must accompany the exe when it is
  distributed.
- **Linux / other:** the system FFmpeg development packages
  (libavformat-dev etc.), found via pkg-config, or any FFmpeg SDK
  folder passed as `-DFFMPEG_ROOT=...`.

## JUCE

Framework (audio devices, audio formats, GUI, VST3 hosting). Fetched by
CMake at configure time (pinned tag); used under JUCE's open-source
(AGPLv3) terms via the free licence held by the project owner. See the
table in [LICENSING.md](LICENSING.md) for JUCE, the Steinberg SDKs and
the reference projects.
