#!/usr/bin/env bash
# Build (and install-test) the Fedora RPM inside a Fedora container, from any
# Linux host with Docker or Podman. Results land in dist/.
#
#   packaging/rpm/build-in-container.sh              # fedora:43
#   packaging/rpm/build-in-container.sh fedora:44    # another Fedora release
#
# If your user can't talk to Docker, run it with sudo; the RPMs are still
# handed back owned by you.
set -euo pipefail

IMAGE="${1:-fedora:43}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENGINE="$(command -v podman || command -v docker || true)"
[[ -n "$ENGINE" ]] || { echo "Docker or Podman is required" >&2; exit 1; }
OWNER="${SUDO_UID:-$(id -u)}:${SUDO_GID:-$(id -g)}"
mkdir -p "$ROOT/dist"

"$ENGINE" run --rm -e OWNER="$OWNER" \
    -v "$ROOT:/src:ro" -v "$ROOT/dist:/out" "$IMAGE" bash -euc '
    dnf -y -q install rpm-build "dnf-command(builddep)" >/dev/null
    dnf -y -q builddep /src/packaging/rpm/snip-and-sketch.spec >/dev/null
    cp -a /src /work
    rm -rf /work/build /work/dist
    out=$(mktemp -d)
    /work/packaging/rpm/build-rpm.sh --out "$out"

    echo "== install test"
    rpm=$(ls "$out"/snip-and-sketch-[0-9]*.$(uname -m).rpm)
    dnf -y -q install "$rpm"
    QT_QPA_PLATFORM=offscreen snip --version
    snip-hotkey --help >/dev/null && echo "snip-hotkey ok"
    test -x /usr/libexec/snip-and-sketch/session-hotkey && echo "session-hotkey ok"
    test -f /etc/xdg/autostart/snip-and-sketch-hotkey.desktop && echo "autostart ok"
    rpm -q --requires snip-and-sketch | grep -i qt6 | sort | head -n 12

    cp "$out"/*.rpm /out/
    chown "$OWNER" /out/*.rpm
'
echo "Done: $(ls -1 "$ROOT"/dist/*.rpm | tr '\n' ' ')"
