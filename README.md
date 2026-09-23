# LILYGO UI Files

English | [简体中文](README.zh-CN.md)

`Files` is an independent LILYGO UI file browser. It is a top-level CMake
project and follows the public interfaces of `lilygo-ui-template`:

- package and executable: `lilygo-ui-files`
- application ID: `cc.lilygo.ui.Files`
- AppKit dependency: `find_package(LilyGoUI CONFIG REQUIRED)`

Application identity, release metadata, permissions, compatibility, build
presets, and deployment defaults are maintained only in `lpm.toml`. CMake
validates that file through LPM and derives generated desktop metadata and
package settings from it. `publish.json` is temporary LPM output and is not a
project configuration file.

The application operates within one configured filesystem root. It supports
directory navigation, current-directory search, read-only text preview, and
creating folders. Copy, rename, move, archive, delete, selection, sort changes,
and view changes are not currently exposed as user actions.

The Files page uses a C++17 `View -> ViewModel -> Model` dependency direction.
`FilesModel` owns filesystem access and domain rules without depending on
LVGL, `FilesViewModel` publishes presentation state through LVGL Subjects, and
`FilesView` creates the responsive LVGL interface and forwards commands. The
interface is implemented directly with LVGL and does not use generated design
sources.

Project-specific architecture and UI requirements are documented in
[`docs/00-overview.md`](docs/00-overview.md) and
[`docs/01-user-interface.md`](docs/01-user-interface.md).

## Prepare AppKit

AppKit is pinned as the `third_party/cm0-appkit` Git submodule. Initialize it
before configuring the application:

```sh
git submodule update --init --recursive
```

The presets select this source SDK with `LilyGoUI_DIR`. Its package config
builds AppKit and LVGL as static libraries and links them into each application.
Updating AppKit code requires updating the submodule revision and rebuilding
the application. A local SDK checkout can be selected explicitly:

```sh
cmake --preset host-simulator -DLilyGoUI_DIR=/path/to/appkit
```

Host builds use the SDK's font assets. Devices install `lilygo-ui-appkit-dev`,
version 0.1.0 or newer, which contains the source SDK and the shared Inter,
Source Han Sans CN, Font Awesome, and font licenses. Applications do not bundle
duplicate fonts. AppKit and LVGL remain statically linked into each application;
the package supplies no AppKit or LVGL shared libraries. The
`min_appkit_version` field in `lpm.toml` sets this package's minimum version.
The package also supports build environments using an installed source SDK
instead of the submodule.

Cross builds require the BSP sysroot and its system development libraries;
they do not require an AppKit SDK package in the sysroot.
When returning from a binary SDK build, use a fresh build directory so no old
imported targets or SDK search paths remain cached.

## Host simulator

```sh
lpm start
```

The stable CMake interface remains available directly:

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
./build/host-simulator/lilygo-ui-files
```

Host simulator builds use `simulated-home/` as their default root so filesystem
changes remain inside the project while debugging. Add or edit files there to
exercise the UI. A different directory can be selected explicitly:

```sh
./build/host-simulator/lilygo-ui-files --root=/tmp/files-demo
```

`LILYGO_UI_FILES_ROOT` provides the same override for launch environments.
The root can also be set persistently with `files.root=/absolute/path` in
`$XDG_CONFIG_HOME/lilygo/ui/files.conf` (or
`$HOME/.config/lilygo/ui/files.conf` when `XDG_CONFIG_HOME` is unset) and
`/etc/lilygo/ui/files.conf`. The precedence is command line, environment,
user configuration, system configuration, then the runtime default. Configured
values must be absolute paths.

The required UI verification viewports are 568x1232 portrait and 1232x568
landscape. The test design also uses 320x568 compact and 1024x768 large
viewports as additional boundary checks; see the UI documentation for the full
acceptance matrix.

## Typography

The application uses `lilygo_ui_font_get()` for all text. Inter, Source Han
Sans CN, and Font Awesome are provided by the shared `lilygo-ui-appkit-dev`
SDK and font package; this application does not bundle duplicate font files.

## Device package

```sh
lpm pack
```

The equivalent lower-level CMake and CPack commands are:

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

The Debian packaging rules install the executable, Launcher manifest, desktop
file, AppStream metadata, app icon, default configuration, and AppKit font
notices. Version, vendor, homepage, target architecture, and the minimum AppKit
dependency are derived from `lpm.toml`.
On the target, the packaged system configuration sets the file root to
`/home/pi`. It can be overridden with `--root`, `LILYGO_UI_FILES_ROOT`, or the
user/system configuration files described above.

## GPU renderer builds

Device rendering is selected at application build time by the source AppKit
SDK. Use an updated SDK supporting `LILYGO_UI_RENDERER` and a target-matched
CM0 sysroot with `libgbm-dev`, `libegl-dev`, and `libgles-dev`. The original
BSP 0.1.0 lacks those GPU development dependencies. The original pinned
AppKit submodule does not support GPU renderer selection; use a newer source
SDK explicitly for this build.

Select the SDK source checkout or the directory containing the installed
source SDK's `LilyGoUIConfig.cmake`:

```sh
cmake --preset cm0-cross -B build/cm0-opengles \
  -DLilyGoUI_DIR=/path/to/appkit \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/cm0-sysroot/usr/share/cm0-bsp/toolchain.cmake \
  -DLILYGO_UI_RENDERER=opengles
cmake --build build/cm0-opengles --parallel
cpack --config build/cm0-opengles/CPackConfig.cmake -B dist/opengles
```

The executable contains AppKit and LVGL code, and its Debian package declares
the selected renderer's system graphics dependencies. Keep separate build
directories for software, OpenGL ES, and experimental NanoVG. Use software
rendering for host simulator tests. Compare rendering correctness, frame time,
memory, and repeated application exit/return on the target before changing a
release renderer.

## Source layout

- `src/main.cpp`: root configuration followed by the AppKit runtime entry point
- `src/app.cpp` and `src/app.hpp`: application lifecycle and Files page assembly
- `src/app_config.cpp` and `src/app_config.hpp`: configuration file parsing
- `src/pages/files/files_model.cpp`: filesystem access, state, and domain rules
- `src/pages/files/files_view_model.cpp`: presentation DTOs, commands, and Subjects
- `src/pages/files/files_view.cpp`: responsive LVGL widgets and event forwarding
- `src/components/`: shared semantic styles and Subject lifetime helpers
- `simulated-home/`: host-only filesystem fixtures for interactive debugging
- `data/`: generic Launcher, desktop, and AppStream templates plus default config
- `tests/`: Model, ViewModel/Subject, and headless responsive render tests

Packaging installs the 128x128 RGBA `assets/app-icon.png`.
