# mini mogumogu 取扱説明書

Minimoog 系のモノフォニック・アナログモデリング・シンセサイザー **mini mogumogu**（VST3 / AU）。3オシレーター、24dB/oct ラダーフィルター、ドライブ／コーラス／ディレイ／リバーブ、16ステップ・シーケンサーを備えています。

## 1. インストール
- **Windows**：`mini-mogumogu-Setup-x.x.x.exe` を実行します。VST3 が `C:\Program Files\Common Files\VST3\` に入ります。DAW でプラグインを再スキャンしてください。署名していないため、初回は Windows が警告を出すことがあります（「詳細情報」→「実行」）。
- **Mac**：`mini-mogumogu-macOS.dmg` を開き、`mini mogumogu.vst3` を `~/Library/Audio/Plug-Ins/VST3/`、`mini mogumogu.component`（AU）を `~/Library/Audio/Plug-Ins/Components/` にコピーします。公証をしていないため、コピーしたあとにターミナルで次を実行してください。
  ```
  xattr -cr "$HOME/Library/Audio/Plug-Ins/VST3/mini mogumogu.vst3"
  xattr -cr "$HOME/Library/Audio/Plug-Ins/Components/mini mogumogu.component"
  ```
  そのあと DAW を再起動（または再スキャン）します。
- 動作環境：Windows 64bit / macOS 10.13 以降（Apple Silicon・Intel 両対応）。VST3 または AU に対応した DAW。
- **インストゥルメント**として読み込み、MIDI トラックから演奏します。

## 2. 基本操作
- **つまみ**：上下にドラッグ。Shift を押しながらで微調整、ダブルクリックで初期値、マウスホイールでも動かせます。下に名前と現在値が出ます。
- **ボタン（セレクター）**：クリックで選択。選ばれている項目はオレンジ色です。
- **ウィンドウ**：右下をドラッグして大きさを変えられます（縦横比 16:9 固定）。
- **鍵盤**：画面の鍵盤（C2〜C6）をクリック／ドラッグで演奏。外部 MIDI キーボードも使えます。PC キーボードは A W S E D F T G Y H U J K O L P ; で音階、Z / X でオクターブ、スペースで RUN / STOP（エディターをクリックしてフォーカスがあるとき）。
- **PITCH / MOD ホイール**：PITCH は離すと中央に戻ります。MOD はそのまま保持されます。
- **PRESET**：左右の `<` `>` で切り替え、中央をクリックで一覧。工場出荷は Init / Fat Bass / Mogu Lead / Brass / Pluck / Sub Thump / Acid Squelch / Sci-Fi S&H / Wobble / Soft Pad。音色の保存は DAW のプリセット機能・プロジェクト保存を使います（音量とシーケンサーの RUN は切り替えても変わりません）。
- **オシロ**：出力波形を表示します。無音のときは横一本線です。
- **VOLUME**：右上のつまみ。

## 3. 各セクション
- **CONTROLLERS**：`TUNE`（全体±100セント）、`GLIDE`（ポルタメント）、`TRIGGER`（LEGATO＝重ねて弾くとエンベロープを再トリガーしない／MULTI＝毎回トリガー）、`BEND`（ピッチベンド幅 ±2 / ±5 / ±12）。
- **OSCILLATORS**：OSC 1〜3 それぞれ `RANGE`（LO / 32' / 16' / 8' / 4' / 2'）と `WAVE`（TRI / SHK / SAW / SQR / PLS1 / PLS2）。OSC 2・3 は `TUNE`（±7半音）。OSC 3 は `KBD CTL` を OFF にすると鍵盤に追従しない低周波発振器として使えます。
- **MIXER**：OSC 1〜3 とノイズの音量。`NOISE` は WHITE / PINK。
- **FILTER**：`CUTOFF`、`EMPHASIS`（レゾナンス）、`CONTOUR`（フィルター EG の効き）、`DRIVE`（フィルター入力の歪み）、`KBD TRACK`（OFF / 1/3 / 2/3 / FULL）。
- **FILTER EG / LOUDNESS EG**：ATTACK / DECAY / SUSTAIN。LOUDNESS EG の `RELEASE` は SHORT（短く切る）／ = DECAY（DECAY の長さで消える）。
- **MODULATION**：`SOURCE`（OSC3 / LFO TRI / LFO SQR / S&H / NOISE）、`RATE`、`PITCH`（ピッチへの深さ）、`FILTER`（カットオフへの深さ）。MOD ホイールを上げると、PITCH / FILTER の深さが最大まで上がります。
- **EFFECTS**：`DRIVE`（AMOUNT）／ CHORUS（RATE・DEPTH・MIX）／ DELAY（TIME・FEEDBACK・TONE・MIX、`SYNC`：FREE は TIME、1/8・1/8.・1/4 は曲のテンポに同期）／ REVERB（SIZE・MIX）。
- **SEQUENCER**：
  - `RUN`：シーケンサーのオン／オフ（HOST SYNC の ON／OFF に関係なく効きます。OFF の間は MIDI 鍵盤で普通に弾けます）。`RND`：ランダムなパターン、`RESET`：初期パターンに戻す。
  - `TEMPO`（HOST SYNC OFF のときのテンポ）、`LENGTH`（1〜16）、`GATE`（音の長さ）、`SWING`、`RATE`（1/8・1/16・1/32）。
  - `DIRECTION`：FWD / REV / PING（行って戻る）/ RND。
  - `KEY>TRANS`：ON のとき、再生中に鍵盤を押すと C3 を基準にパターンを移調します（鍵盤は鳴りません）。`TRANSPOSE` に現在の移調量が出ます。
  - 各ステップ：上の赤い丸が ON/OFF、ノート表示を上下ドラッグ（またはホイール）で C1〜C5、`TIE` でタイ（次のステップへ音をつなぐ）。再生中のステップは光ります。LENGTH より後ろのステップは暗くなります。
- **HOST SYNC**（上部）：シーケンサーのテンポの出どころを選びます。ON ＝ DAW のテンポ・再生位置に合わせます（`RUN` が ON のときに DAW の再生・停止に連動。画面に `CLOCK: DAW` と出ます）。OFF ＝ 内部の `TEMPO` で動きます（`CLOCK: INTERNAL`）。どちらでも、`RUN` が OFF の間はシーケンサーは鳴りません。

## 4. MIDI
ノートオン／オフ、ピッチベンド、CC 1（モジュレーション）、CC 5（GLIDE）、CC 7（VOLUME）、CC 71（EMPHASIS）、CC 72（LOUDNESS DECAY）、CC 73（LOUDNESS ATTACK）、CC 74（CUTOFF）、CC 75（CONTOUR）、CC 91（REVERB MIX）、CC 93（CHORUS MIX）、CC 120 / 123（オールノートオフ）。

## 5. DAW での使い方のヒント
- シーケンサーだけで鳴らすなら、インストゥルメントトラックを作り、`RUN` を ON にして DAW を再生します（HOST SYNC ON）。`RUN` が OFF なら、DAW を再生しても鳴りません。
- つまみは DAW のオートメーションで動かせます（シーケンサーの各ステップのノート／ON／TIE も同様）。
- リバーブとディレイの余韻は、ノートを止めても少し残ります。

## 6. ブラウザ版との違い
MP3 録音、プリセットの JSON 入出力、MIDI デバイス選択はありません。ほかの違いは README を参照してください。
