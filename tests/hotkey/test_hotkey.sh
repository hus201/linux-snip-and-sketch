#!/usr/bin/env bash
# Tests for scripts/snip-hotkey on every supported desktop.
#
# Everything runs in a throw-away sandbox: a temporary HOME/XDG_CONFIG_HOME,
# a private D-Bus session (so dconf/gsettings write to the sandbox, never to
# your real desktop settings), the minimal schemas next to this file, and
# stand-in xfconf-query / kwriteconfig tools that keep their state in files.
#
#   tests/hotkey/test_hotkey.sh            run all tests
#   tests/hotkey/test_hotkey.sh xfce lxqt  run only tests whose name matches
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="${SNIP_HOTKEY:-$HERE/../../scripts/snip-hotkey}"
CMD="/usr/bin/snip --no-editor"

# ------------------------------------------------- sandbox (re-exec once)
if [[ -z "${SNIP_HOTKEY_SANDBOX:-}" ]]; then
    for tool in dbus-run-session gsettings dconf glib-compile-schemas; do
        command -v "$tool" >/dev/null || { echo "SKIP: $tool is not installed"; exit 77; }
    done
    SANDBOX="$(mktemp -d)"
    trap 'rm -rf "$SANDBOX"' EXIT
    mkdir -p "$SANDBOX"/{home,config,data,runtime,schemas,stubs}
    chmod 700 "$SANDBOX/runtime"
    cp "$HERE"/schemas/*.gschema.xml "$SANDBOX/schemas/"
    glib-compile-schemas "$SANDBOX/schemas" || exit 1
    # The environment must be set *before* the private bus starts, so the
    # D-Bus-activated dconf service writes into the sandbox too.
    env -i PATH="$SANDBOX/stubs:$PATH" HOME="$SANDBOX/home" \
        XDG_CONFIG_HOME="$SANDBOX/config" XDG_DATA_HOME="$SANDBOX/data" \
        XDG_DATA_DIRS="$SANDBOX/data:/usr/share" XDG_RUNTIME_DIR="$SANDBOX/runtime" \
        GSETTINGS_SCHEMA_DIR="$SANDBOX/schemas" GSETTINGS_BACKEND=dconf \
        SNIP_HOTKEY_SANDBOX="$SANDBOX" SNIP_HOTKEY="$SCRIPT" LANG=C.UTF-8 \
        dbus-run-session -- bash "$0" "$@"
    exit $?
fi

SANDBOX="$SNIP_HOTKEY_SANDBOX"
STUBS="$SANDBOX/stubs"

# Refuse to run unless writes provably land in the sandbox.
gsettings set org.cinnamon.desktop.keybindings custom-list "['sandbox-check']"
if ! grep -q sandbox-check "$XDG_CONFIG_HOME/dconf/user" 2>/dev/null; then
    echo "FATAL: gsettings did not write into the sandbox; aborting to protect your settings" >&2
    exit 1
fi

# ------------------------------------------------------------ tiny harness
PASS=0
FAIL=0
CURRENT=""
OUT=""
STATUS=0

hk() {  # run snip-hotkey, capturing output and exit status
    OUT="$("$SCRIPT" "$@" --command "$CMD" 2>&1)"
    STATUS=$?
}
ok()   { PASS=$((PASS + 1)); }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL [$CURRENT] $*"; [[ -n "$OUT" ]] && echo "       output: ${OUT//$'\n'/ | }"; }
assert_eq()       { if [[ "$1" == "$2" ]]; then ok; else fail "$3: expected <$2>, got <$1>"; fi; }
assert_status()   { assert_eq "$STATUS" "$1" "exit status"; }
assert_contains() { if [[ "$1" == *"$2"* ]]; then ok; else fail "${3:-text} should contain <$2>; got <$1>"; fi; }
assert_missing()  { if [[ "$1" != *"$2"* ]]; then ok; else fail "${3:-text} should not contain <$2>"; fi; }
assert_file()     { if [[ -e "$1" ]]; then ok; else fail "missing file $1"; fi; }
assert_no_file()  { if [[ ! -e "$1" ]]; then ok; else fail "file should not exist: $1"; fi; }

reset_sandbox() {
    dconf reset -f / 2>/dev/null
    rm -rf "${XDG_CONFIG_HOME:?}"/{lxqt,cosmic,snip-and-sketch,kglobalshortcutsrc} "${XDG_DATA_HOME:?}/applications"
    rm -f "$STUBS"/*.store "$STUBS"/*.log
}

run_test() {
    CURRENT="$1"
    if [[ ${#FILTERS[@]} -gt 0 ]]; then
        local f match=0
        for f in "${FILTERS[@]}"; do [[ "$CURRENT" == *"$f"* ]] && match=1; done
        [[ $match == 1 ]] || return 0
    fi
    reset_sandbox
    local before=$FAIL
    "$1"
    if [[ $FAIL == "$before" ]]; then echo "  ok   $1"; fi
}

# --------------------------------------------------------------- stubs
# xfconf-query: properties kept in a "path=value" file.
cat > "$STUBS/xfconf-query" <<'EOF'
#!/usr/bin/env bash
store="$SNIP_HOTKEY_SANDBOX/stubs/xfconf.store"; touch "$store"
prop=""; value=""; remove=0
while [[ $# -gt 0 ]]; do case "$1" in -p) prop="$2"; shift ;; -s) value="$2"; shift ;; -r) remove=1 ;; esac; shift; done
if [[ $remove == 1 ]]; then grep -vF -- "$prop=" "$store" > "$store.t"; mv "$store.t" "$store"; exit 0; fi
if [[ -n "$value" ]]; then grep -vF -- "$prop=" "$store" > "$store.t"; echo "$prop=$value" >> "$store.t"; mv "$store.t" "$store"; exit 0; fi
line="$(grep -F -- "$prop=" "$store")" || { echo "Property \"$prop\" does not exist" >&2; exit 1; }
echo "${line#*=}"
EOF
# kwriteconfig5/6: log every call.
for v in 5 6; do
    printf '#!/usr/bin/env bash\necho "$*" >> "$SNIP_HOTKEY_SANDBOX/stubs/kwriteconfig%s.log"\n' "$v" > "$STUBS/kwriteconfig$v"
done
chmod +x "$STUBS"/*

# ================================================================= tests
xfce_get() { grep -F -- "$1=" "$STUBS/xfconf.store" 2>/dev/null | cut -d= -f2-; }
CIN=org.cinnamon.desktop.keybindings
CIN_REL="$CIN.custom-keybinding:/org/cinnamon/desktop/keybindings/custom-keybindings"
GNOME=org.gnome.settings-daemon.plugins.media-keys
GNOME_REL="$GNOME.custom-keybinding:/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
MATE_REL="org.mate.control-center.keybinding:/org/mate/desktop/keybindings"

# --- Cinnamon ------------------------------------------------------------
test_cinnamon_install() {
    hk install --desktop cinnamon
    assert_status 0
    assert_eq "$(gsettings get $CIN custom-list)" "['custom0']" "custom-list"
    assert_eq "$(gsettings get $CIN_REL/custom0/ name)" "'Snip & Sketch for Linux'" "name"
    assert_eq "$(gsettings get $CIN_REL/custom0/ command)" "'$CMD'" "command"
    assert_eq "$(gsettings get $CIN_REL/custom0/ binding)" "['<Shift><Super>s']" "binding"
}

test_cinnamon_idempotent_and_keeps_others() {
    gsettings set $CIN custom-list "['__dummy__', 'custom0']"
    gsettings set $CIN_REL/custom0/ name 'Terminal'
    gsettings set $CIN_REL/custom0/ command 'gnome-terminal'
    hk install --desktop cinnamon
    hk install --desktop cinnamon
    assert_status 0
    assert_eq "$(gsettings get $CIN custom-list)" "['__dummy__', 'custom0', 'custom1']" "custom-list"
    assert_eq "$(gsettings get $CIN_REL/custom0/ command)" "'gnome-terminal'" "other shortcut untouched"
    assert_eq "$(gsettings get $CIN_REL/custom1/ command)" "'$CMD'" "our command"
}

test_cinnamon_reinstall_announces_list_change() {
    # Cinnamon only re-reads a command when custom-list changes, so updating
    # an existing entry must still produce a custom-list change.
    hk install --desktop cinnamon
    gsettings set $CIN_REL/custom0/ command '/old/path/snip --no-editor'
    local log="$SANDBOX/monitor.log"
    gsettings monitor $CIN custom-list > "$log" 2>&1 &
    local mon=$!
    sleep 0.3
    hk install --desktop cinnamon
    sleep 0.3
    kill "$mon" 2>/dev/null; wait "$mon" 2>/dev/null
    assert_eq "$(gsettings get $CIN_REL/custom0/ command)" "'$CMD'" "command updated"
    assert_contains "$(cat "$log")" "custom-list" "custom-list change notification"
}

test_cinnamon_conflict_needs_force() {
    gsettings set $CIN.wm switch-to-workspace-1 "['<Shift><Super>s']"
    hk install --desktop cinnamon
    assert_status 1
    assert_contains "$OUT" "already used"
    assert_eq "$(gsettings get $CIN custom-list)" "@as []" "nothing added on conflict"
    hk install --desktop cinnamon --force
    assert_status 0
    assert_eq "$(gsettings get $CIN custom-list)" "['custom0']" "added with --force"
}

test_cinnamon_remove_and_status() {
    gsettings set $CIN custom-list "['__dummy__']"
    hk install --desktop cinnamon
    hk status --desktop cinnamon
    assert_contains "$OUT" "$CMD" "status"
    hk remove --desktop cinnamon
    assert_status 0
    assert_eq "$(gsettings get $CIN custom-list)" "['__dummy__']" "custom-list after remove"
    hk status --desktop cinnamon
    assert_contains "$OUT" "not installed" "status after remove"
}

test_cinnamon_renames_entry_with_old_name() {
    # Development builds named the shortcut "Snip & Sketch": reuse and rename it, no duplicate.
    gsettings set $CIN custom-list "['__dummy__', 'custom0']"
    gsettings set $CIN_REL/custom0/ name 'Snip & Sketch'
    gsettings set $CIN_REL/custom0/ command '/home/me/.local/bin/snip --no-editor'
    hk install --desktop cinnamon
    assert_status 0
    assert_eq "$(gsettings get $CIN custom-list)" "['__dummy__', 'custom0']" "no duplicate"
    assert_eq "$(gsettings get $CIN_REL/custom0/ name)" "'Snip & Sketch for Linux'" "renamed"
    assert_eq "$(gsettings get $CIN_REL/custom0/ command)" "'$CMD'" "command updated"
    hk remove --desktop cinnamon
    assert_eq "$(gsettings get $CIN custom-list)" "['__dummy__']" "removable"
}

test_cinnamon_renames_entry_with_other_old_name() {
    # ...and briefly "Linux Snip & Sketch": also reused and renamed.
    gsettings set $CIN custom-list "['__dummy__', 'custom0']"
    gsettings set $CIN_REL/custom0/ name 'Linux Snip & Sketch'
    hk install --desktop cinnamon
    assert_eq "$(gsettings get $CIN custom-list)" "['__dummy__', 'custom0']" "no duplicate"
    assert_eq "$(gsettings get $CIN_REL/custom0/ name)" "'Snip & Sketch for Linux'" "renamed"
}

# --- GNOME ---------------------------------------------------------------
test_gnome_install_remove() {
    hk install --desktop gnome
    assert_status 0
    local path=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/snip-and-sketch/
    assert_eq "$(gsettings get $GNOME custom-keybindings)" "['$path']" "custom-keybindings"
    assert_eq "$(gsettings get $GNOME_REL/snip-and-sketch/ binding)" "'<Shift><Super>s'" "binding"
    assert_eq "$(gsettings get $GNOME_REL/snip-and-sketch/ command)" "'$CMD'" "command"
    hk install --desktop gnome
    assert_eq "$(gsettings get $GNOME custom-keybindings)" "['$path']" "no duplicate"
    hk remove --desktop gnome
    assert_eq "$(gsettings get $GNOME custom-keybindings)" "@as []" "removed"
}

test_gnome_keeps_existing_and_detects_conflict() {
    local other=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/
    gsettings set $GNOME custom-keybindings "['$other']"
    gsettings set org.gnome.desktop.wm.keybindings switch-to-workspace-1 "['<Super><Shift>s']"
    hk install --desktop gnome
    assert_status 1
    hk install --desktop gnome --force
    assert_status 0
    assert_contains "$(gsettings get $GNOME custom-keybindings)" "'$other'" "existing entry kept"
    hk remove --desktop gnome
    assert_eq "$(gsettings get $GNOME custom-keybindings)" "['$other']" "only ours removed"
}

test_gnome_renames_entry_with_old_name() {
    local path=/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/snip-and-sketch/
    gsettings set $GNOME custom-keybindings "['$path']"
    gsettings set $GNOME_REL/snip-and-sketch/ name 'Snip & Sketch'
    hk install --desktop gnome
    assert_eq "$(gsettings get $GNOME custom-keybindings)" "['$path']" "no duplicate"
    assert_eq "$(gsettings get $GNOME_REL/snip-and-sketch/ name)" "'Snip & Sketch for Linux'" "renamed"
}

# --- MATE ----------------------------------------------------------------
test_mate_install_remove() {
    gsettings set $MATE_REL/custom0/ name 'Terminal'
    hk install --desktop mate
    assert_status 0
    assert_eq "$(gsettings get $MATE_REL/custom1/ action)" "'$CMD'" "uses next free slot"
    assert_eq "$(gsettings get $MATE_REL/custom1/ binding)" "'<Mod4><Shift>s'" "binding"
    hk install --desktop mate
    assert_eq "$(dconf list /org/mate/desktop/keybindings/ | tr '\n' ' ')" "custom0/ custom1/ " "no duplicate"
    hk remove --desktop mate
    assert_eq "$(dconf list /org/mate/desktop/keybindings/ | tr '\n' ' ')" "custom0/ " "only ours removed"
}

# --- Xfce ----------------------------------------------------------------
test_xfce_install_conflict_remove() {
    local prop='/commands/custom/<Shift><Super>s'
    hk install --desktop xfce
    assert_status 0
    assert_eq "$(xfce_get "$prop")" "$CMD" "property"
    hk install --desktop xfce
    assert_status 0
    echo "$prop=thunar" > "$STUBS/xfconf.store"
    hk install --desktop xfce
    assert_status 1
    assert_eq "$(xfce_get "$prop")" "thunar" "other app's binding kept"
    hk install --desktop xfce --force
    assert_eq "$(xfce_get "$prop")" "$CMD" "replaced with --force"
    hk remove --desktop xfce
    assert_eq "$(xfce_get "$prop")" "" "removed"
}

# --- KDE -----------------------------------------------------------------
# KDE binds shortcuts to a .desktop file; these use a fake system data dir so
# the result doesn't depend on whether the package is installed here.
kde_sysdata() { rm -rf "$SANDBOX/sysdata"; mkdir -p "$SANDBOX/sysdata/applications"; }

test_kde6_install_remove() {
    kde_sysdata  # package not installed: the script must provide the .desktop file
    XDG_DATA_DIRS="$SANDBOX/sysdata" KDE_SESSION_VERSION=6 hk install --desktop kde
    assert_status 0
    assert_contains "$(cat "$STUBS/kwriteconfig6.log")" \
        "--file kglobalshortcutsrc --group services --group snip-and-sketch-new.desktop --key _launch Meta+Shift+S"
    local desktop="$XDG_DATA_HOME/applications/snip-and-sketch-new.desktop"
    assert_file "$desktop"
    assert_contains "$(cat "$desktop")" "Exec=$CMD" "desktop file"
    XDG_DATA_DIRS="$SANDBOX/sysdata" KDE_SESSION_VERSION=6 hk remove --desktop kde
    assert_no_file "$desktop"
    assert_contains "$(cat "$STUBS/kwriteconfig6.log")" "--delete"
}

test_kde_uses_packaged_desktop_file() {
    kde_sysdata  # package installed: reuse its .desktop file, write no user copy
    touch "$SANDBOX/sysdata/applications/snip-and-sketch-new.desktop"
    XDG_DATA_DIRS="$SANDBOX/sysdata" KDE_SESSION_VERSION=6 hk install --desktop kde
    assert_status 0
    assert_no_file "$XDG_DATA_HOME/applications/snip-and-sketch-new.desktop"
}

test_kde_custom_command_overrides_packaged_file() {
    kde_sysdata  # e.g. the AppImage: a user copy must point at the custom command
    touch "$SANDBOX/sysdata/applications/snip-and-sketch-new.desktop"
    XDG_DATA_DIRS="$SANDBOX/sysdata" KDE_SESSION_VERSION=6 \
        "$SCRIPT" install --desktop kde --command "/opt/Snip.AppImage --no-editor" >/dev/null 2>&1
    assert_contains "$(cat "$XDG_DATA_HOME/applications/snip-and-sketch-new.desktop" 2>/dev/null)" \
        "Exec=/opt/Snip.AppImage --no-editor" "user desktop file"
}

test_kde5_writes_plasma5_format() {
    KDE_SESSION_VERSION=5 hk install --desktop kde
    assert_status 0
    assert_contains "$(cat "$STUBS/kwriteconfig5.log")" \
        "--group snip-and-sketch-new.desktop --key _launch Meta+Shift+S,none,Snip & Sketch for Linux: New snip"
    assert_missing "$(cat "$STUBS/kwriteconfig5.log")" "--group services" "Plasma 5 format"
}

# --- LXQt ----------------------------------------------------------------
test_lxqt_install_remove_keeps_others() {
    local conf="$XDG_CONFIG_HOME/lxqt/globalkeyshortcuts.conf"
    mkdir -p "$(dirname "$conf")"
    printf '[Control%%2BAlt%%2BT.1]\nComment=Terminal\nEnabled=true\nExec=qterminal\n' > "$conf"
    local original; original="$(cat "$conf")"
    hk install --desktop lxqt
    hk install --desktop lxqt
    assert_status 0
    assert_eq "$(grep -c '^Comment=Snip & Sketch for Linux' "$conf")" "1" "exactly one entry"
    assert_contains "$(cat "$conf")" "Exec=/usr/bin/snip, --no-editor" "exec line"
    assert_contains "$(cat "$conf")" "Exec=qterminal" "other shortcut kept"
    hk remove --desktop lxqt
    assert_eq "$(cat "$conf")" "$original" "file restored"
}

test_lxqt_renames_entry_with_old_name() {
    local conf="$XDG_CONFIG_HOME/lxqt/globalkeyshortcuts.conf"
    mkdir -p "$(dirname "$conf")"
    printf '[Shift%%2BMeta%%2BS.700]\nComment=Snip & Sketch\nEnabled=true\nExec=/old/snip, --no-editor\n' > "$conf"
    hk install --desktop lxqt
    assert_status 0
    assert_eq "$(grep -c '^Comment=' "$conf")" "1" "replaced, not duplicated"
    assert_contains "$(cat "$conf")" "Comment=Snip & Sketch for Linux"
    assert_missing "$(cat "$conf")" "/old/snip" "old command"
}

# --- COSMIC --------------------------------------------------------------
test_cosmic_install_remove_keeps_others() {
    local file="$XDG_CONFIG_HOME/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom"
    mkdir -p "$(dirname "$file")"
    printf '{\n    (\n        modifiers: [\n            Super,\n        ],\n        key: "t",\n    ): Spawn("kitty"),\n}\n' > "$file"
    local original; original="$(cat "$file")"
    hk install --desktop cosmic
    hk install --desktop cosmic
    assert_status 0
    assert_eq "$(grep -c 'Spawn("/usr/bin/snip' "$file")" "1" "exactly one entry"
    assert_contains "$(cat "$file")" 'Spawn("kitty")' "other shortcut kept"
    assert_eq "$(tail -n 1 "$file")" "}" "map still closed"
    hk remove --desktop cosmic
    assert_eq "$(cat "$file")" "$original" "file restored"
}

test_cosmic_creates_file() {
    hk install --desktop cosmic
    local file="$XDG_CONFIG_HOME/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom"
    assert_eq "$(head -n 1 "$file")" "{" "starts a map"
    assert_contains "$(cat "$file")" "key: \"s\"" "binding"
}

# --- general -------------------------------------------------------------
test_detects_desktop_from_environment() {
    OUT="$(XDG_CURRENT_DESKTOP=X-Cinnamon "$SCRIPT" status 2>&1)"
    assert_contains "$OUT" "Cinnamon:" "X-Cinnamon"
    OUT="$(XDG_CURRENT_DESKTOP=ubuntu:GNOME "$SCRIPT" status 2>&1)"
    assert_contains "$OUT" "GNOME:" "ubuntu:GNOME"
    OUT="$(XDG_CURRENT_DESKTOP=XFCE "$SCRIPT" status 2>&1)"
    assert_contains "$OUT" "Xfce:" "XFCE"
    OUT="$(XDG_CURRENT_DESKTOP=Budgie:GNOME "$SCRIPT" status 2>&1)"
    assert_contains "$OUT" "GNOME:" "Budgie"
}

test_tiling_wm_prints_instructions() {
    hk install --desktop sway
    assert_status 2
    assert_contains "$OUT" "bindsym Mod4+Shift+s exec $CMD"
    assert_contains "$OUT" "bind = SUPER SHIFT, S, exec, $CMD"
}

test_once_marker() {
    hk install --desktop xfce --once
    assert_file "$XDG_CONFIG_HOME/snip-and-sketch/hotkey-configured"
    hk remove --desktop xfce
    hk install --desktop xfce --once  # must not come back after the user removed it
    assert_eq "$(xfce_get '/commands/custom/<Shift><Super>s')" "" "--once respects removal"
}

test_rejects_unknown_arguments() {
    hk install --bogus
    assert_status 1
    assert_contains "$OUT" "unknown argument"
}

# ================================================================= main
FILTERS=("$@")
echo "snip-hotkey tests ($SCRIPT)"
for t in $(declare -F | awk '{print $3}' | grep '^test_'); do
    run_test "$t"
done
echo "$PASS assertions passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
