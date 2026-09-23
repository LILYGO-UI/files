# Third-party dependencies

`cm0-appkit` is the pinned AppKit Git submodule configured in the repository
root. Initialize it with `git submodule update --init --recursive`. This
application loads its source SDK through `find_package(LilyGoUI CONFIG REQUIRED)`;
the SDK config builds AppKit and LVGL static libraries into the application.
Do not copy SDK sources into the application's source tree.
