# YM2000 v1.0.0

アナログモデリング・シンセサイザー **YM2000**（VST3 / AU）の最初の公開版です。無料で使えます。

## ダウンロード
- **Windows**：`YM2000-Setup-1.0.0.exe`（VST3 を `C:\Program Files\Common Files\VST3\` に入れます）
- **Mac**：`YM2000-macOS.dmg`（VST3 と AU が入っています。Apple Silicon / Intel 両対応のビルド）

## インストール
- **Windows**：インストーラーを実行し、DAW でプラグインを再スキャンします。署名していないため、Windows が「不明な発行元」と警告を出すことがあります（「詳細情報」→「実行」で進めます）。
- **Mac**：dmg を開き、`YM2000.vst3` を `~/Library/Audio/Plug-Ins/VST3/`（または `/Library/Audio/Plug-Ins/VST3/`）、`YM2000.component` を `~/Library/Audio/Plug-Ins/Components/` にコピーします。公証をしていないため、コピーしたあとにターミナルで次を実行してください。
  ```
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/YM2000.vst3
  xattr -cr ~/Library/Audio/Plug-Ins/Components/YM2000.component
  ```
  そのあと DAW を再起動（または再スキャン）します。

## 主な機能
- 4音ポリ、OSC×2、フィルター4種、EG×2、LFO×2、Virtual Patch×4
- アルペジエーター、Mod Sequence、16バンド・ヴォコーダー（DAW のサイドチェイン入力）
- Mod FX、Delay FX、EQ、リミッター
- **HOST SYNC**：アルペジエーター・Mod Seq・LFO1 を DAW のテンポに同期
- 工場音色8種、全パラメーターが DAW のオートメーション対象

## 動作確認の状況
- Mac（Apple Silicon）の Ableton Live 12 で動作を確認しています。
- Windows の DAW、Intel の Mac、Live 以外の DAW、AU 版での確認は十分ではありません。不具合があれば、DAW の名前とバージョン、OS、操作手順を添えて連絡してください。

## ライセンス
フリーウェアです。詳細は `LICENSE.txt` を参照してください。
