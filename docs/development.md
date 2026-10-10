# Development

## Build

```bash
sudo apt install cmake ninja-build g++ qt6-base-dev qt6-base-dev-tools libqt6svg6-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cd build && cpack -G DEB        # .deb
```

Fedora RPM: `packaging/rpm/build-rpm.sh` on Fedora, or `packaging/rpm/build-in-container.sh` anywhere with Docker.

## Test

```bash
ctest --test-dir build --output-on-failure
```

Suites: `canvas`, `overlay`, `editor`, `launch` (Qt, off-screen), `hotkey` (sandboxed; never touches your settings), `packaging`.

After UI changes, refresh the README images: `tests/update-screenshots.sh build`.

## Release

Bump the version in `CMakeLists.txt` and `packaging/rpm/snip-and-sketch.spec`, then:

```bash
git tag v1.0.0 && git push origin v1.0.0
```

CI runs only on tags: it tests, builds the `.deb` and `.rpm`, and publishes them.
