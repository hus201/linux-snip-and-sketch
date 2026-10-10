#!/usr/bin/env bash
# Keeps the packaging in sync with the build:
#  - the RPM spec's Version matches project(VERSION) in CMakeLists.txt
#  - every file `cmake --install` produces is listed in the spec's %files
#  - paths used by the .deb maintainer scripts are really installed
#  - the maintainer scripts and helper are valid sh
#
#   tests/packaging/test_packaging.sh <cmake-build-dir>
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:?usage: $0 <cmake-build-dir>}"
SPEC="$ROOT/packaging/rpm/snip-and-sketch.spec"
FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

# --- versions
cmake_version="$(grep -m1 -oP '^\s*VERSION \K[0-9.]+' "$ROOT/CMakeLists.txt")"
spec_version="$(awk '/^Version:/ {print $2; exit}' "$SPEC")"
[[ "$cmake_version" == "$spec_version" ]] \
    || fail "spec Version $spec_version != CMakeLists.txt VERSION $cmake_version"
grep -q -- "- $cmake_version-" "$SPEC" || fail "spec %changelog has no entry for $cmake_version"

# --- installed files vs %files
DEST="$(mktemp -d)"
trap 'rm -rf "$DEST"' EXIT
DESTDIR="$DEST" cmake --install "$BUILD" --prefix /usr >/dev/null || fail "cmake --install failed"
files="$(sed -n '/^%files/,/^%changelog/p' "$SPEC")"
while IFS= read -r path; do
    rel="/${path#"$DEST"/}"
    case "$rel" in /usr/share/doc/*) continue ;; esac  # %license/%doc in RPM, copyright in .deb
    macro="$rel"
    macro="${macro/#\/usr\/bin/%\{_bindir\}}"
    macro="${macro/#\/usr\/libexec/%\{_libexecdir\}}"
    macro="${macro/#\/usr\/share/%\{_datadir\}}"
    macro="${macro/#\/etc/%\{_sysconfdir\}}"
    grep -qF -- "$macro" <<<"$files" || fail "installed $rel is missing from the spec's %files ($macro)"
done < <(find "$DEST" -type f -o -type l | sort)

# --- paths the .deb scripts rely on
for script in "$ROOT"/packaging/debian/postinst "$ROOT"/packaging/debian/prerm; do
    sh -n "$script" || fail "$(basename "$script") is not valid sh"
    for used in $(grep -oE '/usr/(bin|libexec)/[A-Za-z0-9/_.-]+' "$script" | sort -u); do
        [[ -x "$DEST$used" ]] || fail "$(basename "$script") uses $used, which isn't installed"
    done
done
sh -n "$DEST/usr/libexec/snip-and-sketch/session-hotkey" || fail "session-hotkey is not valid sh"
grep -q '@CMAKE' "$DEST/usr/libexec/snip-and-sketch/session-hotkey" && fail "session-hotkey was not configured"
grep -qx '/etc/xdg/autostart/snip-and-sketch-hotkey.desktop' "$ROOT/packaging/debian/conffiles" \
    || fail "debian/conffiles doesn't list the autostart file"

[[ $FAIL -eq 0 ]] && echo "packaging consistent (version $cmake_version)"
exit $FAIL
