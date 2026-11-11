# YM Echo — テスト用 VST3 ディレイ

YM シリーズの DAW「YM ORPHEUS」の M2（VST3 ホスティング）動作確認のために作った、
小さなステレオ・ディレイの VST3 エフェクトプラグインです。
「YM Echo がスキャンに出て、挿して、エコーが聞こえる」= ホスト側が正しく動いている、
という確認に使うのが目的です。自作コードのみで実装しています。

Cakewalk Sonar など、他の VST3 ホストでも通常のエフェクトとして使えます。

## パラメーター

| パラメーター | 範囲 | 初期値 | 内容 |
|---|---|---|---|
| Time | 10–1500 ms | 375 ms | ディレイタイム。スムージング付きでノイズなく変化します |
| Feedback | 0.0–0.85 | 0.35 | 繰り返し量。ループ内に 1-pole ローパスが入っていて、繰り返すほど少し暗くなります |
| Mix | 0.0–1.0 | 0.30 | ウェット音量（ドライはそのまま 1.0 で通過し、ウェットを加算します） |
| Ping-Pong | on/off | off | オンでフィードバックが L↔R を往復します |

- ステレオ入出力（モノ入出力にも対応）
- テール長は 2 秒としてホストに報告
- 状態は APVTS 経由で保存/復元（ホストのプロジェクトに残ります）

## ビルド（Windows）

前提: Visual Studio 2022 / 2026（「C++によるデスクトップ開発」ワークロード）と Git。

一番簡単なのは `build.bat` をダブルクリックする方法です（YM ORPHEUS と同じ方式）。
Visual Studio の検出、環境設定、CMake 構成、ビルドまで自動で行い、
ログは `build-log.txt` に残ります。初回は JUCE のダウンロードが入ります。

手動の場合:

```
cmake -B build -G "Visual Studio 17 2022" -A x64 -T v143
cmake --build build --config Release
```

（VS 2026 の場合はジェネレーター `"Visual Studio 18 2026"` / ツールセット `v145`）

成果物:

```
build\YMEcho_artefacts\Release\VST3\YM Echo.vst3
```

`YM Echo.vst3` はファイルではなくフォルダーです。フォルダーごと
`C:\Program Files\Common Files\VST3` へコピーしてください（管理者権限の確認が出ます）。

## 使い方（YM ORPHEUS M2 で確認する場合）

1. 上記の通り `YM Echo.vst3` を `C:\Program Files\Common Files\VST3` に置く
2. YM ORPHEUS を起動し、「Scan for Plugins...」を実行
3. トラックの FX に「YM Echo」を追加
4. 再生するとエコーが聞こえること、Time / Feedback / Mix が効くことを確認

## 構成

```
ym-echo/
  CMakeLists.txt            … JUCE 8.0.15 を FetchContent で取得（タグ固定、ORPHEUS と同じ）
  build.bat                 … Windows 用ワンクリックビルド
  Source/
    PluginProcessor.h/.cpp  … ディレイ本体（juce::dsp::DelayLine、線形補間）
    PluginEditor.h/.cpp     … ダークパネルの GUI（ノブ×3＋Ping-Pong トグル）
```

## ライセンスについて

YM Echo は自作コードのみで書かれており、YM ORPHEUS と同じく
GPL 系ライセンスでのオープンソース公開を前提とした計画の一部です。
JUCE はそのオープンソース利用条件のもとで使用しています。
VST3 は Steinberg の商標です。
