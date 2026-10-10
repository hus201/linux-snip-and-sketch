# RPM package for Fedora (also usable on COPR).
#
#   Build:  packaging/rpm/build-rpm.sh            (needs rpmbuild)
#           packaging/rpm/build-in-container.sh   (any distro with Docker/Podman)
#
# Keep Version in sync with project(VERSION) in CMakeLists.txt — the
# "packaging" test fails when they differ.

Name:           snip-and-sketch
Version:        1.0.0
Release:        1%{?dist}
Summary:        Screenshot snipping and annotation tool (Win+Shift+S)
License:        MIT
URL:            https://github.com/hus201/linux-snip-and-sketch
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  desktop-file-utils
BuildRequires:  cmake(Qt6Core) >= 6.2
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  cmake(Qt6Test)

Requires:       hicolor-icon-theme
# Window snip on X11 (lists windows)
Recommends:     xprop
Recommends:     xwininfo
# Screen capture on Wayland
Recommends:     xdg-desktop-portal
Recommends:     qt6-qtwayland

%description
Snip & Sketch for Linux captures a rectangle, a freeform shape, a window or
the whole screen, copies it to the clipboard and lets you mark it up with a
pen, highlighter, eraser and crop tool before saving.

The package registers the Shift+Super+S (Win+Shift+S) hotkey on Cinnamon,
GNOME, KDE Plasma, Xfce, MATE, LXQt and COSMIC. Use snip-hotkey to change
or remove it.

%prep
%autosetup -p1

%build
%cmake -DSNIP_INSTALL_DEBIAN_COPYRIGHT=OFF -DSNIP_BUILD_TESTS=ON
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/snip-and-sketch.desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/snip-and-sketch-new.desktop
# The Qt test suites run off-screen; the hotkey-script suite needs a D-Bus
# session and is covered by the project's CI instead.
export QT_QPA_PLATFORM=offscreen
%ctest --label-regex unit

%post
# Set the hotkey right away for logged-in users (also refreshes it on upgrade).
%{_libexecdir}/snip-and-sketch/session-hotkey install >/dev/null 2>&1 || :

%preun
if [ "$1" -eq 0 ]; then
    # Erase, not upgrade: don't leave a hotkey pointing at a missing program.
    %{_libexecdir}/snip-and-sketch/session-hotkey remove >/dev/null 2>&1 || :
    pkill -x snip >/dev/null 2>&1 || :
fi

%files
%license LICENSE
%doc README.md
%{_bindir}/snip
%{_bindir}/snip-hotkey
%dir %{_libexecdir}/snip-and-sketch
%{_libexecdir}/snip-and-sketch/session-hotkey
%{_datadir}/applications/snip-and-sketch.desktop
%{_datadir}/applications/snip-and-sketch-new.desktop
%{_datadir}/icons/hicolor/scalable/apps/snip-and-sketch.svg
%config(noreplace) %{_sysconfdir}/xdg/autostart/snip-and-sketch-hotkey.desktop

%changelog
* Sun Oct 11 2026 Snip & Sketch for Linux contributors - 1.0.0-1
- Initial release: rectangle, freeform, window and fullscreen snips
- Editor with pen, highlighter, eraser, crop, zoom and pan
- Optional "expand canvas when drawing outside the image"
- Win+Shift+S hotkey on Cinnamon, GNOME, KDE, Xfce, MATE, LXQt and COSMIC
