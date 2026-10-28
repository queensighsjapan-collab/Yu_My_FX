# YM2000 取扱説明書

アナログモデリング・シンセサイザー **YM2000**（VST3 / AU）。4音ポリ、2オシレーター、16バンド・ヴォコーダー、アルペジエーター、Mod Sequence を備えています。

![GUI](gui.png)

## 1. インストール
- **Windows**：`YM2000-Setup-x.x.x.exe` を実行します。VST3 が `C:\Program Files\Common Files\VST3\` に入ります。DAW でプラグインを再スキャンしてください。署名していないため、初回は Windows が警告を出すことがあります（「詳細情報」→「実行」）。
- **Mac**：`YM2000-macOS.dmg` を開き、`YM2000.vst3` を `~/Library/Audio/Plug-Ins/VST3/`、`YM2000.component`（AU）を `~/Library/Audio/Plug-Ins/Components/` にコピーします。公証をしていないため、コピーしたあとにターミナルで次を実行してください。
  ```
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/YM2000.vst3
  xattr -cr ~/Library/Audio/Plug-Ins/Components/YM2000.component
  ```
  そのあと DAW を再起動（または再スキャン）します。
- 動作環境：Windows 64bit / macOS 10.13 以降（Apple Silicon・Intel 両対応のビルド）。VST3 または AU に対応した DAW。

## 2. 基本操作
- **つまみ**：上下にドラッグして調整します。Shift を押しながらドラッグすると微調整、ダブルクリックで初期値、マウスホイールでも動かせます。
- **セレクター**（波形やタイプの切り替え）：つまみの周りに文字が並んでいます。上下ドラッグで切り替えます。選ばれている項目はオレンジ色になります。
- **LCD**：触ったパラメーターの名前と値を表示します。何も触っていないときは、現在の音色名を表示します。
- **ウィンドウ**：右下をドラッグすると大きさを変えられます（縦横比は固定）。
- **鍵盤**：画面の鍵盤をクリックして音を出せます。外部の MIDI キーボードも使えます。
- **ホイール**：左がピッチベンド（離すと中央に戻る）、右がモジュレーションです。
- **OCTAVE SHIFT**（− / ＋）：画面の鍵盤の音域を上下に動かします。

## 3. 各セクション
- **OSC 1**：`WAVE`（SAW / PULSE / TRI / SIN / VOX / DWGS / NOISE / AUDIO IN）、`CTRL 1`、`CTRL 2`（波形ごとに変わる調整）、`DWGS`（DWGS 波形の番号 1〜64）。`AUDIO IN` は DAW の入力音声をオシレーターの代わりに使います。
- **OSC 2**：`WAVE`（SAW / SQR / TRI）、`MOD SELECT`（OFF / RING / SYNC / R+S）、`SEMITONE`、`TUNE`。
- **MIXER**：OSC 1、OSC 2、NOISE の音量。
- **FILTER**：`TYPE`（24 LPF / 12 LPF / BPF / HPF）、`CUTOFF`、`RESO`、`EG1 INT`（EG 1 の効き具合）、`VELO`（ベロシティの効き具合）、`KBD TRK`（鍵盤の高さに応じた変化）。
- **AMP**：`LEVEL`、`PAN`、`DIST`（ディストーション）、`KBD TRK`。
- **EG 1 / EG 2**：ATTACK / DECAY / SUSTAIN / RELEASE。EG 1 はフィルター、EG 2 はアンプに主に使われます。
- **LFO 1 / LFO 2**：`WAVE`、`FREQ`。LFO 1 は `KEY SYNC`（鍵盤を押すたびに頭から始める）と `TEMPO SYNC`（FREQ を曲のテンポに同期）、LFO 2 は `KEY SYNC` があります。
- **VIRTUAL PATCH**：4本の接続を自由に作れます。`SOURCE`（EG1 / EG2 / LFO1 / LFO2 / VELOCITY / P.BEND / MOD WHL / KEY TRK）から `DESTINATION`（PITCH / OSC2 PCH / OSC1 CT1 / NOISE / CUTOFF / AMP / PAN / LFO2 FRQ）へ、`INTENSITY` で効き具合を決めます。
- **MOD FX**：`TYPE`（CHO/FLG / ENS / PHASER）、`SPEED`、`DEPTH`。
- **DELAY FX**：`TYPE`（STEREO / CROSS / L/R）、`TIME`、`DEPTH`。
- **PITCH**：`TUNE`、`BEND RANGE`（ピッチベンドの幅）、`TRANSP`（移調）、`VIB INT`（モジュレーションホイールのビブラート量）、`PORTA`（ポルタメント）。
- **EQ**：`HI FREQ`、`HI GAIN`、`LOW FREQ`、`LOW GAIN`。
- **MASTER**：`VOLUME`、`AUDIO IN 1 / 2`（入力音声のレベル）、`PROGRAM`（音色の切り替え）。
- **ARPEGGIATOR**：`ON/OFF`、`LATCH`（鍵盤を離しても続ける）、`TYPE`（UP / DOWN / ALT1 / ALT2 / RANDOM / TRIGGER）、`RANGE`（1〜4 オクターブ）、`GATE TIME`、`TEMPO`。
- **VOCODER**：`VOICE MODE`（SINGLE / VOCODER）、`FORMANT SHIFT`、`CLARITY`、`CH LEVEL` / `CH PAN`（16バンドごとの音量とパン。バーをクリック・ドラッグして描く）。
- **MOD SEQUENCE**：16ステップのバーで、選んだ宛先を順番に動かします。`ON/OFF`、`KEY SYNC`（鍵盤を押すたびに1番から）。

## 4. 音色（プログラム）
`PROGRAM` つまみで工場音色を呼び出せます：Init Prog / Bass / Pad / Sync Lead / Arp Pluck / Vox Lead / Vocoder / Mod Seq（計8種）。DAW のプログラムチェンジでも切り替えられます。

自分で作った音色は、DAW のプロジェクトとプリセット機能に保存されます（プラグイン内に独自の保存スロットはありません）。

## 5. ヴォコーダーの使い方
1. `VOICE MODE` を `VOCODER` にします（工場音色の「Vocoder」を呼び出すのが簡単です）。
2. しゃべり声やドラムなど、モジュレーターにしたい音声を DAW のトラックに用意し、**YM2000 のサイドチェイン入力**に送ります。送り方は DAW によって違います。DAW のサイドチェインの説明を参照してください。
3. 鍵盤で和音を弾くと、入力音声の特徴が音に乗ります。`CLARITY` で子音の聞こえ方、`FORMANT SHIFT` で声の太さを変えます。

## 6. HOST SYNC（DAW のテンポに同期）
ARPEGGIATOR の見出しの右にあるスイッチです。
- **OFF**（初期値）：アルペジエーター・Mod Seq・LFO 1 のテンポ同期は、`TEMPO` つまみの値で動きます。
- **ON**：DAW のテンポに追従します。再生中は拍の頭に揃います。DAW がテンポを渡さない場合は `TEMPO` つまみの値に戻ります。
HOST SYNC は音色ではなく、プラグイン全体の設定として保存されます。

## 7. MIDI
ノート、ピッチベンド、CC1（モジュレーション）、CC64（サスティン）、CC120 / 123（オールノートオフ）、プログラムチェンジに対応しています。パラメーターは DAW のオートメーションで操作できます。

## 8. 既知の制限
- 画面から音色の保存・呼び出しをするスロットはありません（DAW のプリセットを使います）。
- MIDI の最低音フィルターとオクターブシフトはありません。移調は `TRANSP` または DAW 側で行ってください。
- ヴォコーダーの声は、マイクやファイルではなく、DAW のサイドチェイン入力から入れます。

## 9. ライセンス
無料で使えます。詳細は `LICENSE.txt` を参照してください。VST は Steinberg Media Technologies GmbH の登録商標です。KORG および MS2000 は KORG Inc. の商標で、本ソフトウェアとは無関係です。
