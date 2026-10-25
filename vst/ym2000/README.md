# YM2000 (VST3 / AU)

`synthesizer/ms2000-synth.html`（Yu_My_FX）の音源を VST3 / AU プラグイン化したものです。JUCE + C++。**ソースは1つ**で、Windows 用と Mac 用はビルドするだけで作り分けます（`.github/workflows/ym2000-build.yml` が両方を自動ビルド）。

## できること（HTML版と同じ）
- 4音ポリ、OSC1（SAW/PULSE/TRI/SIN/VOX/DWGS/NOISE/AUDIO IN）、OSC2（RING/SYNC）、フィルター4種、EG×2、LFO×2
- Virtual Patch×4、Mod Sequence、アルペジエーター、16バンド・ヴォコーダー
- Mod FX（CHO/FLG・ENS・PHASER）、Delay FX、EQ、リミッター
- パラメーターは全部 DAW のオートメーション対象（約 150 個）。音色は DAW のプロジェクトとプリセットに保存
- MIDI：ノート、ピッチベンド、CC1(モジュレーション)、CC64(サスティン)、CC120/123(オールノートオフ)、プログラムチェンジ(工場音色8種)

## HTML版との違い
- GUI は未実装（DAW 標準の汎用パラメーター画面）。後で JUCE で実機風パネルを作る予定
- ヴォコーダーの声（モジュレーター）は、MIC/FILE/DEMO ではなく **DAW の入力（サイドチェイン）**。`Audio In / Sidechain` を有効にしてルーティングする
- アルペジエーター／Mod Seq のテンポは `Arp / Seq Tempo` パラメーター（ホストのテンポ同期は未対応）
- 発音ノート範囲の制限やオクターブシフトのボタンは無し（ノート番号をそのまま使用。`Pitch Transpose` あり）
- 遅延（レイテンシ）6ms（リミッターのルックアヘッド。DAW が自動補正）

## ビルド
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target YM2000_VST3      # Mac は YM2000_AU も
```
成果物：`build/YM2000_artefacts/Release/VST3/YM2000.vst3`
GitHub に push すれば Actions（`.github/workflows/build.yml`）が Windows / macOS(Universal) 用を作り、Artifacts からダウンロードできます。

### 置き場所
- Windows: `C:\Program Files\Common Files\VST3\`
- Mac: `/Library/Audio/Plug-Ins/VST3/`（AU は `.../Components/`）。ダウンロードしたものは `xattr -cr YM2000.vst3` が必要な場合あり（未署名のため）

## 検証
- `tests/run_tests.sh <ms2000-synth.html>`：音源エンジンを HTML 版（JS）と同じ演奏で比較 → 23パターンで**ビット一致**（誤差 4e-9 以下）
- FX は本物の Chromium の Web Audio で元グラフを動かして比較 → 誤差 0.3〜3%（音量差 0.1dB 前後）
- `pluginval` strictness 5 合格（Linux）、`tests/PluginSmoke.cpp`（`-DYM_BUILD_TESTS=ON`）で発音・アルペジエーター・プログラム・状態保存を確認
- Windows / Mac での実機ビルド・DAW 動作確認は未実施（CI でビルドを確認してください）

## ライセンス注意
JUCE は AGPLv3 または商用ライセンスです。自分用で使う分には問題ありませんが、バイナリを配布する場合は JUCE のライセンス条件（ソース公開 or JUCE ライセンス購入）に従ってください。
