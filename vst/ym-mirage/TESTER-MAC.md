# YM Mirage（Mac版）テストのお願い

## YM Mirage って何？

粒々グラニュラー＋FREEZEのエフェクト（VST3プラグイン）です。音を細かく刻んで雲のように散らしたり、瞬間を凍らせてパッドのように伸ばしたりできます。
Apple Silicon / Intel 両対応のユニバーサルバイナリです。

## インストール方法

1. zip を解凍して、**"YM Mirage.vst3"** を次のフォルダにコピーしてください
（管理者パスワードを求められます）

```
/Library/Audio/Plug-Ins/VST3/
```

※ ユーザーフォルダ配下の `~/Library/Audio/Plug-Ins/VST3/` でも大丈夫です。

2. **ターミナルで次のコマンドを1回実行してください。**
このプラグインは開発中のテスト版で、Appleの公証（ノータリゼーション）を受けていないため、この作業をしないとDAWに読み込まれません。

```
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/YM Mirage.vst3"
```

※ `~/Library/...` に入れた場合はこちらです：

```
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/YM Mirage.vst3"
```

3. DAWを再起動（またはプラグインの再スキャン）をして、適当なトラックに **"YM Mirage"** を挿してください。

## 聴いてみてほしいポイント

素材はボーカルやシンセパッドなど、持続音が分かりやすいです。

1. **FREEZE**：押すとその瞬間の音が凍って伸びるか。離すと元の流れに戻るか。
2. **PITCH**：±12半音まで上下するか（MIDI CC1でも動きます）。
3. **DENSITY / SIZE / SMEAR**：粒の感じが変わるか。ノイズや音切れがないか。
4. **Bypass**：MIDI CC65（既定）やパネルのON/OFFで、音が途切れず切り替わるか。

気づいたこと（鳴らない、画面が変、効果が弱い/強すぎる等）があれば、スクリーンショットや録音と一緒に教えてください。よろしくお願いします！
