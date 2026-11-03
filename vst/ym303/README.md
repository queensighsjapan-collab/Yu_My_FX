# YM303 (VST3 / AU)

`synthesizer/yu-my-303.html`（TB-303風アシッドベース＋16ステップシーケンサー）を VST3 / AU プラグイン化したものです。JUCE + C++。ソースは1つで、Windows 用と Mac 用は GitHub Actions が自動でビルドします。

**現在は最初の版です。** 音源エンジンとシーケンサーは入っていますが、専用の画面（横長の実機風パネル）はまだありません。DAW 標準のパラメーター画面で操作します。

## できること
- 音源：ブラウザ版と同じ（のこぎり波／矩形波、18dB/oct ラダーフィルター、アクセントの積み上がり、スライド）
- シーケンサー：32パターン（グループ I〜IV × 1〜8）、各パターンは A / B の2セクション（各 1〜16 ステップ）、トラック（曲）再生、A / B / A→B
- **HOST SYNC ON（既定）**：`RUN` が ON のとき、DAW の再生／停止とテンポに合わせて内蔵シーケンサーが鳴ります（`RUN` が OFF なら鳴りません）。再生位置の16分音符のグリッドに合うので、DAW の途中から再生しても小節頭にそろいます
- **HOST SYNC OFF**：`Run` と `Tempo`（60〜200）で単体でも鳴ります。Standalone 版もこちらです
- パターン・トラック・つまみの位置は DAW のプロジェクトに保存されます

## 操作（パラメーター画面）
| パラメーター | 内容 |
|---|---|
| Tuning / Cut Off Freq / Resonance / Env Mod / Decay / Accent / Volume | 音色（0〜10）。ブラウザ版のつまみと同じ |
| Waveform | Saw / Square |
| Tempo, Run, Host Sync | クロック（上記） |
| Play Mode | Pattern（選んだパターン）／ Track（トラックの並び順） |
| Patt. Group, Pattern | 鳴らす（編集する）パターンの選択（I-1 〜 IV-8） |
| Play Section | A / B / A>B（A>B のとき編集対象は A） |
| Length | 選択中セクションのステップ数（1〜16） |
| Step 01〜16 Note / Octave / Accent / Slide | 選択中セクションの各ステップ。Note は Rest（休符）/ Tie（タイ）/ C〜C+ |

ステップのパラメーターは「選択中のパターン・セクション」の中身を表します。Patt. Group / Pattern / Play Section を切り替えると、その内容に入れ替わります。

## 今の版でできないこと
- 専用画面（次の版で、ブラウザ版と同じ横長パネルを予定）
- トラックの編集（初期は I-1, I-2, I-3 の順）。ブラウザ版のトラック書き込みに当たる操作は画面と一緒に追加します
- 録音（DAW の録音を使ってください）

## HTML版との違い
- 出力にブラウザ版のコンプレッサーはありません。代わりに 0.8 を超えるピークだけを緩やかに丸めます（通常の演奏では変わりません）
- 遅延（レイテンシ）は 0

## ビルド
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target YM303_VST3      # Mac は YM303_AU も
```
成果物：`build/YM303_artefacts/Release/VST3/YM303.vst3`。GitHub に push すれば Actions（`.github/workflows/ym303-build.yml`）が Windows / macOS(Universal) 用を作り、Artifacts からダウンロードできます。

## 検証
`tests/run_tests.sh <yu-my-303.html>`
- 音源エンジンを、ブラウザ版の JS（`AcidCore`）と同じ演奏で比較：13パターンで誤差 1e-9 以下（多くはビット一致）
- シーケンサーを、ブラウザ版の**実際のスケジューラー**（Chromium で動かして Audio303.note の呼び出しを記録）と比較：パターン5種＋トラックで、音・タイ・スライド・アクセント・ゲートの長さが一致（タイミング誤差 1サンプル以内）。内部クロックと DAW クロック、ブロックサイズ違いでも同じ結果
- `tests/PluginSmoke.cpp`（`-DYM_BUILD_TESTS=ON`）：プラグイン全体（パラメーター、DAW 同期、編集ビュー、状態保存、極端な設定）
- `pluginval` strictness 5（エディターを開くテストを含む）合格（Linux）


## GUI(v0.2)

ブラウザ版と同じ横長パネルを専用GUIとして実装しました(固定比率でリサイズ可能、ウィンドウ幅 624〜1872px)。

- ノブ: 縦ドラッグ(Shiftで微調整)、ホイール、ダブルクリックで初期値。操作中は上部のステップモニターに値を表示
- 鍵盤・PITCH/TIME MODE・DOWN/UP/ACCENT/SLIDE・A/B/A→B・BACK/WRITE-NEXT・DEL/INS・LEN・PATTERN CLEAR はブラウザ版と同じ操作
- MODE(TRACK WRITE/PLAY, PATTERN PLAY/WRITE)で TRACK リストの作成・編集も可能
- PCキーボード: A W S E D F T G Y H U J K で音階、スペースで RUN/STOP(エディタにフォーカスがある時)
- RUN/STOP: 内蔵シーケンサーの入/切。HOST SYNC ON ではDAWの再生に追従(RUN が ON の間だけ鳴る)、OFF では TEMPO で単体動作。右下の HOST SYNC ボタンで切替
- 埋め込みフォント: Barlow Condensed / Share Tech Mono(SIL OFL 1.1、`Fonts/OFL-*.txt`)

`tests/GuiShot.cpp`: ヘッドレスでエディタをPNG出力(`YM303_GuiShot out.png [幅] [0|1]`)。状態 2 でキーボード操作テストを実行します。

## v0.2.1 の変更

- RUN/STOP が HOST SYNC 中も有効(DAW再生中でも STOP で内蔵シーケンサーが止まる)。RUN の初期値は OFF
- ホストの MIDI ノートでモノフォニック発音(ベロシティ100以上でアクセント、重なるノートはスライド)。内蔵シーケンサーが鳴っている間はシーケンサーを優先
- HOST SYNC をボタン表示に変更し、切替時にステップモニターに説明を表示
