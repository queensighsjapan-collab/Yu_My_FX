# MeronTronIII（Mac版）テストのお願い

## MeronTronIII って何？

2013年に作られたメロトロン風キーボード音源を、13年ぶりに復元したテープリプレイ楽器です（VST3プラグイン＋単体アプリ）。
音色は5種類（Strings / Flute / Choir / Brass / Violin）、鍵盤は35鍵（G2〜F5）です。
かつて音が出なかった2つの鍵（Flute の A#3、Strings の D5）も修理済みです。
Apple Silicon / Intel 両対応のユニバーサルバイナリです。

## まず単体アプリで試すのが簡単です

1. zip を解凍して、**"MeronTronIII.app"** をアプリケーションフォルダにコピーしてください。
2. **ターミナルで次のコマンドを1回実行してください。**
   このアプリは開発中のテスト版で、Appleの公証（ノータリゼーション）を受けていないため、この作業をしないと起動を止められます。

```bash
xattr -dr com.apple.quarantine "/Applications/MeronTronIII.app"
```

3. アプリを起動して、画面下の鍵盤をクリックするか、Macのキーボード（A〜Kの列など）で弾いてみてください。

## VST3 を DAW で使う場合

1. **"MeronTronIII.vst3"** を次のフォルダにコピーしてください（管理者パスワードを求められます）

```
/Library/Audio/Plug-Ins/VST3/
```

※ ユーザーフォルダ配下の `~/Library/Audio/Plug-Ins/VST3/` でも大丈夫です。

2. ターミナルで quarantine 除去を1回実行してください。

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/MeronTronIII.vst3"
```

※ `~/Library/...` に入れた場合はこちらです：

```bash
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/MeronTronIII.vst3"
```

3. DAWを再起動（またはプラグインの再スキャン）をして、インストゥルメントとして **"MeronTronIII"** を挿してください。

## 聴いてみてほしいポイント

1. **音色5種**：左下のボタンで切り替えて、それぞれ鳴るか。
2. **長押し**：鍵盤を長く押さえて、音が途中で切れたりせず最後まで伸びるか（Windows版で直した箇所です）。
3. **直した2音**：Flute の A#3（一番下の G から数えて16番目の鍵）と、Strings の D5（一番上から2番目の白鍵）がちゃんと鳴るか。周りの音となじんでいるか。
4. **HALF SPEED**：スイッチを ON にすると1オクターブ低く、ゆっくり再生されるか。
5. **和音**：数音同時に押して鳴るか、音が割れないか。
