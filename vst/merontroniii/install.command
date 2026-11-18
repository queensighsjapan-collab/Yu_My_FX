#!/bin/bash
# MeronTronIII (Mac) かんたんインストーラー
# VST3 / AU と スタンドアロンアプリを所定の場所にコピーして、検疫（quarantine）を解除します。
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"

echo "==============================================="
echo "  MeronTronIII インストーラー"
echo "==============================================="
echo ""

# --- VST3 -> ~/Library/Audio/Plug-Ins/VST3/ ---
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$VST3_DEST"
if [ -d "$DIR/MeronTronIII.vst3" ]; then
  rm -rf "$VST3_DEST/MeronTronIII.vst3"
  ditto "$DIR/MeronTronIII.vst3" "$VST3_DEST/MeronTronIII.vst3"
  xattr -dr com.apple.quarantine "$VST3_DEST/MeronTronIII.vst3" 2>/dev/null
  echo "✓ VST3 をコピーしました: $VST3_DEST/MeronTronIII.vst3"
else
  echo "× MeronTronIII.vst3 が見つかりませんでした（このファイルと同じフォルダに置いてください）"
fi
echo ""

# --- AU -> ~/Library/Audio/Plug-Ins/Components/ ---
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$AU_DEST"
if [ -d "$DIR/MeronTronIII.component" ]; then
  rm -rf "$AU_DEST/MeronTronIII.component"
  ditto "$DIR/MeronTronIII.component" "$AU_DEST/MeronTronIII.component"
  xattr -dr com.apple.quarantine "$AU_DEST/MeronTronIII.component" 2>/dev/null
  echo "✓ AU をコピーしました: $AU_DEST/MeronTronIII.component"
else
  echo "× MeronTronIII.component が見つかりませんでした（このファイルと同じフォルダに置いてください）"
fi
echo ""

# --- Standalone app -> /Applications (書けない場合は ~/Applications) ---
if [ -w "/Applications" ]; then
  APP_DEST="/Applications"
else
  APP_DEST="$HOME/Applications"
  mkdir -p "$APP_DEST"
fi
if [ -d "$DIR/MeronTronIII.app" ]; then
  rm -rf "$APP_DEST/MeronTronIII.app"
  ditto "$DIR/MeronTronIII.app" "$APP_DEST/MeronTronIII.app"
  xattr -dr com.apple.quarantine "$APP_DEST/MeronTronIII.app" 2>/dev/null
  echo "✓ アプリをコピーしました: $APP_DEST/MeronTronIII.app"
else
  echo "× MeronTronIII.app が見つかりませんでした（このファイルと同じフォルダに置いてください）"
fi

echo ""
echo "インストール完了です。"
echo "・スタンドアロン: $APP_DEST の MeronTronIII.app を起動してください（画面の鍵盤や Mac のキーボードで演奏できます）"
echo "・VST3: DAW を再起動（または再スキャン）すると MeronTronIII が読み込まれます"
echo "・AU: Logic Pro を再起動すると、AU 音源 → YM → MeronTronIII から選べます"
echo ""
echo "Enter キーを押すとこのウィンドウを閉じます。"
read -r _
