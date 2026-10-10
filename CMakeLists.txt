cmake_minimum_required(VERSION 3.16)
project(snip-and-sketch
    VERSION 1.0.0
    DESCRIPTION "Snip & Sketch for Linux — capture, annotate and share screenshots"
    HOMEPAGE_URL "https://github.com/hus201/linux-snip-and-sketch"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_INSTALL_DOCDIR share/doc/snip-and-sketch)  # Debian: /usr/share/doc/<package>
include(GNUInstallDirs)
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Qt 6.2 is what Ubuntu 22.04 / Debian 12 ship — keep that as the floor.
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets DBus Network Svg)

# Everything except main() lives in a static library shared with the tests.
add_library(snipcore STATIC
    src/controller.cpp  src/controller.h
    src/capture.cpp     src/capture.h
    src/windowlist.cpp  src/windowlist.h
    src/overlay.cpp     src/overlay.h
    src/canvas.cpp      src/canvas.h
    src/editor.cpp      src/editor.h
    src/icons.cpp       src/icons.h
    src/inkpopup.cpp    src/inkpopup.h)
target_include_directories(snipcore PUBLIC src)
target_compile_definitions(snipcore PUBLIC
    SNIP_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING QT_DISABLE_DEPRECATED_BEFORE=0x060000)
target_compile_options(snipcore PUBLIC -Wall -Wextra)
target_link_libraries(snipcore PUBLIC Qt6::Widgets Qt6::DBus Qt6::Network Qt6::Svg)

add_executable(snip src/main.cpp data/resources.qrc)
target_link_libraries(snip PRIVATE snipcore)

option(SNIP_BUILD_TESTS "Build the test suites" ON)
if(SNIP_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    # One executable per suite; run them all with `ctest --test-dir build`.
    foreach(suite canvas overlay editor launch)
        add_executable(test_${suite} tests/test_${suite}.cpp tests/testutil.h)
        target_link_libraries(test_${suite} PRIVATE snipcore Qt6::Test)
        add_test(NAME ${suite} COMMAND test_${suite})
        set_tests_properties(${suite} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS unit)
    endforeach()
    # The hotkey script, against every desktop, in a sandboxed D-Bus session.
    add_test(NAME hotkey COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/hotkey/test_hotkey.sh)
    set_tests_properties(hotkey PROPERTIES SKIP_RETURN_CODE 77 LABELS script)
    # .deb/.rpm metadata in sync with what the build installs.
    add_test(NAME packaging COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/packaging/test_packaging.sh ${CMAKE_BINARY_DIR})
    set_tests_properties(packaging PROPERTIES LABELS script)
endif()

# ------------------------------------------------------------------- install
install(TARGETS snip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS scripts/snip-hotkey DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/snip-and-sketch.desktop data/snip-and-sketch-new.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/snip-and-sketch.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/snip-and-sketch-hotkey.desktop
        DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)
# Helper the .deb/.rpm install scripts run as root to set the hotkey for
# logged-in users right away.
configure_file(packaging/session-hotkey.in session-hotkey @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/session-hotkey
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/snip-and-sketch)
# Debian wants the license as /usr/share/doc/<pkg>/copyright; the RPM spec
# ships LICENSE with %license instead and turns this off.
option(SNIP_INSTALL_DEBIAN_COPYRIGHT "Install packaging/debian/copyright into the doc dir" ON)
if(SNIP_INSTALL_DEBIAN_COPYRIGHT)
    install(FILES packaging/debian/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# ---------------------------------------------------------------- packaging
set(CPACK_PACKAGE_NAME "snip-and-sketch")
set(CPACK_PACKAGE_VENDOR "Snip & Sketch for Linux contributors")
set(CPACK_PACKAGE_CONTACT "Hussein <husain.anabtawi.2@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Screenshot snipping and annotation tool (Win+Shift+S)")
set(CPACK_PACKAGE_DESCRIPTION
"Snip & Sketch for Linux captures a rectangle, a freeform shape, a window or
the whole screen, copies it to the clipboard and lets you mark it up with a
pen, highlighter, eraser and crop tool before saving.
.
The package registers the Shift+Super+S (Win+Shift+S) hotkey on Cinnamon,
GNOME, KDE Plasma, Xfce, MATE, LXQt and COSMIC. Use `snip-hotkey` to change
or remove it.")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Shared-library deps are computed by dpkg-shlibdeps (needs Qt from apt);
# turn it off when building against a non-packaged Qt.
option(SNIP_DEB_SHLIBDEPS "Compute .deb library dependencies with dpkg-shlibdeps" ON)
set(SNIP_RUNTIME_DEPENDS "qt6-qpa-plugins")  # the xcb/wayland platform plugins
if(SNIP_DEB_SHLIBDEPS)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${SNIP_RUNTIME_DEPENDS}")
else()
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6widgets6 (>= 6.2), libqt6gui6 (>= 6.2), libqt6core6 (>= 6.2), libqt6dbus6 (>= 6.2), libqt6network6 (>= 6.2), libqt6svg6 (>= 6.2), libc6, libstdc++6, ${SNIP_RUNTIME_DEPENDS}")
endif()
# x11-utils: window snip on X11 · qt6-wayland: native Wayland
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "x11-utils, qt6-wayland, xdg-desktop-portal")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/prerm;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/conffiles")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_STRIP_FILES ON)
set(CPACK_GENERATOR "DEB")
include(CPack)
