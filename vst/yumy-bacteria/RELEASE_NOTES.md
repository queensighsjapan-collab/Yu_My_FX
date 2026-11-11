# YuMy Bacteria v0.1.0

Access Virus TI 風のバーチャルアナログ・シンセサイザー **YuMy Bacteria**（VST3 / AU）の最初の公開版です。

## ダウンロード
- **Windows**：`YuMyBacteria-Setup-0.1.0.exe`（VST3 を `C:\Program Files\Common Files\VST3\` に入れます）
- **Mac**：`YuMyBacteria-macOS.dmg`（VST3 と AU が入っています。Apple Silicon / Intel 両対応のビルド）

## インストール
- **Windows**：インストーラーを実行し、DAW でプラグインを再スキャンします。署名していないため、Windows が「不明な発行元」と警告を出すことがあります（「詳細情報」→「実行」で進めます）。
- **Mac**：dmg を開き、`YuMy Bacteria.vst3` を `~/Library/Audio/Plug-Ins/VST3/`、`YuMy Bacteria.component` を `~/Library/Audio/Plug-Ins/Components/` にコピーします。公証をしていないため、コピーしたあとにターミナルで次を実行してください。
  ```
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/"YuMy Bacteria.vst3"
  xattr -cr ~/Library/Audio/Plug-Ins/Components/"YuMy Bacteria.component"
  ```
  そのあと DAW を再起動（または再スキャン）します。

## 主な機能
- OSC×2（CLASSIC / HYPERSAW / WAVETABLE）、サブ・ノイズ・FM・リングモジュレーター、ユニゾン最大7ボイス
- フィルター×2（LP/HP/BP/BS、SERIAL / PARALLEL / SPLIT）、EG×2、LFO×3（テンポ同期・フェード等）
- モジュレーション・マトリクス（6スロット × 3デスティネーション）
- アルペジエーター、エフェクト（ディレイ / リバーブ / コーラス・フランジャー / ディストーション / フェイザー / リングモッド / EQ / フォルマント）
- ヴォコーダー（8 / 16 / 32バンド）：モジュレーターは DAW のサイドチェイン入力
- ランダム生成（RANDOMIZE / MUTATE / BACK / LOCK）、工場音色38個
- **HOST SYNC**：アルペジエーター・LFO・ディレイを DAW のテンポに同期
- 全パラメーターが DAW のオートメーション対象、画面は拡大縮小可

## 仕様上の注意
- 1インスタンス = 1パートです（HTML版のマルチは載せていません。複数の音色は DAW のトラックを分けて使います）。
- ユーザープリセットは DAW のプロジェクトに保存されます。
- アルペジエーターは ARP ON のうえで鍵盤を押している間（または HOLD）だけ動きます。

## 動作確認の状況
- エンジンは HTML 版との波形一致、Windows / macOS ビルド、Linux でのスモークテストと pluginval（strictness 5）を CI で確認しています。
- 実際の DAW での動作確認はまだ十分ではありません（特にサイドチェイン・ヴォコーダーの実音、AU 版）。不具合があれば、DAW の名前とバージョン、OS、操作手順を添えて連絡してください。

## ライセンス
使用許諾契約書（`LICENSE.txt`）を参照してください。
