# YM Vortex（Mac版）テストのお願い

## YM Vortex って何？

複数段オールパス＋デュアルLFOのフェイザー（VST3プラグイン）です。深くうねるスウィープと、強いFEEDBACKの自己発振気味の唸りが持ち味です。
Apple Silicon / Intel 両対応のユニバーサルバイナリです。

## インストール方法

1. zip を解凍して、**"YM Vortex.vst3"** を次のフォルダにコピーしてください
（管理者パスワードを求められます）

```
/Library/Audio/Plug-Ins/VST3/
```

※ ユーザーフォルダ配下の `~/Library/Audio/Plug-Ins/VST3/` でも大丈夫です。

2. **ターミナルで次のコマンドを1回実行してください。**
このプラグインは開発中のテスト版で、Appleの公証（ノータリゼーション）を受けていないため、この作業をしないとDAWに読み込まれません。

```
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/YM Vortex.vst3"
```

※ `~/Library/...` に入れた場合はこちらです：

```
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/YM Vortex.vst3"
```

3. DAWを再起動（またはプラグインの再スキャン）をして、適当なトラックに **"YM Vortex"** を挿してください。

## 聴いてみてほしいポイント

素材はシンセパッドやギター、ボーカルなど持続音が分かりやすいです。

1. **RATE / DEPTH**：うねりの速さと深さが変わるか。STAGES（4/6/8/12段）で質感が変わるか。
2. **FEEDBACK**：強くすると唸りが増すか。音が割れたり止まらなくならないか。
3. **WIDTH**：ステレオの広がりが変わるか。
4. **Bypass**：MIDI CC65（既定）やパネルのON/OFFで、音が途切れず切り替わるか。

気づいたこと（鳴らない、画面が変、効果が弱い/強すぎる等）があれば、スクリーンショットや録音と一緒に教えてください。よろしくお願いします！
