# YuMy Bacteria (VST3 / AU / Standalone)

`synthesizer/yumy-bacteria.html`（Access Virus TI 風シンセ）の VST 版です。JUCE 8 / C++17。

## 構成
- `Source/Engine.*` — HTML 版エンジン（AudioWorklet の `Core`）の C++ 移植。JUCE 非依存。HTML 版と同じ乱数・同じ演算順序で、出力が一致します（`tests/run_tests.sh`）。
- `Source/Tables.inc` — パラメーター表・オプション・ファクトリープリセット38個。`tools/gen_tables.js` が HTML から生成します。HTML 側を変えたら再生成してください。
- `Source/PluginProcessor.*` — パラメーター（APVTS）、MIDI、DAW テンポ同期、サイドチェイン入力（ボーコーダーのモジュレーター）。
- `Source/PluginEditor.*` — 暫定のエディター（汎用パラメーター一覧）。パネル GUI は次の段階。

## 仕様（VST 版）
- シングルパート（1 インスタンス = 1 音色）。マルチは DAW のトラックで代用します。
- ボーコーダーの入力は DAW のサイドチェイン（モノ／ステレオ。ステレオはモノに混ぜます）。
- ユーザープリセットは DAW のプロジェクト保存に任せます。
- アルペジエーターは ARP ON のうえ鍵盤を押している間（HOLD 時はラッチ中）だけ鳴ります。HOST TEMPO SYNC をオンにすると DAW のテンポで動き、DAW が止まると止まります。
- 対応 MIDI: ノート／ベロシティ、ピッチベンド、CC1、CC64、CC120/123、チャンネルプレッシャー、プログラムチェンジ。ノブの CC 割り当ては DAW のオートメーションで行います。

## テスト
- `tests/run_tests.sh /path/to/yumy-bacteria.html` — C++ エンジンと HTML 版の波形比較（g++, node, python3+numpy）。
- `YuMyBacteria_SmokeTest`（`-DYB_BUILD_TESTS=ON`）— プラグイン全体のスモークテスト。`vst/DESIGN_RULES.md` の「鳴りっぱなし」ガードを含みます。CI（`vst-tests.yml`）で毎回実行されます。

## GUI (stage b)
- HTML版パネルを再現したカスタムGUI(つまみ=縦ドラッグ / Shiftで微調整 / ダブルクリックでリセット / DAWオートメーション連動)。
- 画面は拡大縮小可(縦横比固定)。「SHOW SOUND DESIGN」で VOCODER / EFFECTS EXTRA / MOD MATRIX / ARP SETTINGS / LFO SETUP / OSC EXTRA を開閉。
- LCD: プログラム名(PREV/NEXT・PROGRAMセレクト)と、触ったパラメーター名・値。
- RANDOMIZE / MUTATE / BACK / LOCK はHTML版と同じロジックをC++へ移植(`Source/Randomizer.h`)。
- ヴォコーダーのモジュレーターはDAWのサイドチェイン入力。INPUT LEVELメーターで入力有無を確認。
- GUIスクリーンショット: `YuMyBacteria_GuiShot <out.png> [program] [fold 0|1] [width]`(CIで生成)。
