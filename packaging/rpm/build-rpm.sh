#!/usr/bin/env bash
# Build the RPMs from this source tree with rpmbuild.
#   packaging/rpm/build-rpm.sh [--out DIR]     (default: dist/)
# Needs rpmbuild and the BuildRequires from the spec; on a Debian/Ubuntu host
# use build-in-container.sh instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SPEC="$ROOT/packaging/rpm/snip-and-sketch.spec"
OUT="$ROOT/dist"
[[ "${1:-}" == "--out" ]] && OUT="$(mkdir -p "$2" && cd "$2" && pwd)"

command -v rpmbuild >/dev/null || { echo "rpmbuild not found (try build-in-container.sh)" >&2; exit 1; }
NAME="$(rpmspec -q --qf '%{name}\n' "$SPEC" 2>/dev/null | head -n1 || awk '/^Name:/ {print $2; exit}' "$SPEC")"
VERSION="$(awk '/^Version:/ {print $2; exit}' "$SPEC")"

TOP="$(mktemp -d)"
trap 'rm -rf "$TOP"' EXIT
mkdir -p "$TOP"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}

# Source tarball of the working tree (uncommitted changes included), without
# build output.
tar -C "$ROOT/.." \
    --exclude-vcs --exclude="./$(basename "$ROOT")/build" --exclude="./$(basename "$ROOT")/dist" \
    --exclude="./$(basename "$ROOT")/AppDir" \
    --transform "s|^\./$(basename "$ROOT")|$NAME-$VERSION|" \
    -czf "$TOP/SOURCES/$NAME-$VERSION.tar.gz" "./$(basename "$ROOT")"

rpmbuild -ba --define "_topdir $TOP" "$SPEC"

mkdir -p "$OUT"
find "$TOP/RPMS" "$TOP/SRPMS" -name '*.rpm' ! -name '*-debug*' -exec cp {} "$OUT/" \;
echo "RPMs in $OUT:"
ls -1 "$OUT"/*.rpm
