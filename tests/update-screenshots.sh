#!/usr/bin/env bash
# Regenerates the README screenshots in docs/screenshots/ from the test
# suites' demo scenes (a fake desktop and report — never your real screen).
#
#   tests/update-screenshots.sh [build-dir]     (default: build)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build}" && pwd)"
OUT="$ROOT/docs/screenshots"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export QT_QPA_PLATFORM=offscreen SNIP_SCREENSHOT_DIR="$TMP"
"$BUILD/test_overlay" screenshots >/dev/null
"$BUILD/test_editor" screenshots >/dev/null

mkdir -p "$OUT"
for name in overlay-window overlay-rect editor dark-editor crop palette; do
    cp "$TMP/$name.png" "$OUT/$name.png"
done
echo "Updated: $(cd "$OUT" && ls *.png | tr '\n' ' ')"
