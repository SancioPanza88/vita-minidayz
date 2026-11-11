#!/bin/sh
# Downloads the Mini DayZ HTML5 export into ./game (not stored in this repository).
# Usage: tools/fetch_game.sh [plus|original|reloaded]
set -e
VARIANT=${1:-plus}
case "$VARIANT" in
  plus) SUB=docs ;;
  original) SUB=original_docs ;;
  reloaded) SUB=reloaded_docs ;;
  *) echo "unknown variant $VARIANT"; exit 1 ;;
esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
git clone --depth 1 https://github.com/NextDev65/MiniDayZ "$TMP/src"
git -C "$TMP/src" log -1 --format='upstream commit %H' 
rm -rf "$ROOT/game"
cp -r "$TMP/src/$SUB" "$ROOT/game"
rm -rf "$TMP"
cd "$ROOT/game"
# web/cordova-only files the native runtime does not need
rm -rf plugins cordova-js-src cordova.js cordova_plugins.js sw.js offline.js offlineClient.js \
       platform.js exec.js confighelper.js config.xml manifest.json appmanifest.json index.html \
       jquery-*.min.js favicon.ico
find . -name '*.m4a' -delete
echo "game ($VARIANT) ready in $ROOT/game: $(find . -type f | wc -l) files, $(du -sh . | cut -f1)"
