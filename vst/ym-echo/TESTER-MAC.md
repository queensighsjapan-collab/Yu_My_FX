# YM Echo（Mac版）テストのお願い

## YM Echo って何？

シンプルなステレオ・ディレイ（VST3プラグイン）です。テスト用の軽いエフェクトです。
Apple Silicon / Intel 両対応のユニバーサルバイナリです。

## インストール方法

1. zip を解凍して、**"YM Echo.vst3"** を次のフォルダにコピーしてください
（管理者パスワードを求められます）

```
/Library/Audio/Plug-Ins/VST3/
```

※ ユーザーフォルダ配下の `~/Library/Audio/Plug-Ins/VST3/` でも大丈夫です。

2. **ターミナルで次のコマンドを1回実行してください。**
このプラグインは開発中のテスト版で、Appleの公証（ノータリゼーション）を受けていないため、この作業をしないとDAWに読み込まれません。

```
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/YM Echo.vst3"
```

※ `~/Library/...` に入れた場合はこちらです：

```
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/YM Echo.vst3"
```

3. DAWを再起動（またはプラグインの再スキャン）をして、適当なトラックに **"YM Echo"** を挿してください。

## 聴いてみてほしいポイント

1. ディレイが鳴るか、TIME / FEEDBACK / MIX が効くか。
2. ノイズや音切れがないか、CPUに異常な負荷がないか。

気づいたこと（鳴らない、画面が変等）があれば、スクリーンショットや録音と一緒に教えてください。よろしくお願いします！
