# Application assets

`app-icon.svg` is the editable source. `app-icon.png` is its 128x128 RGBA
Launcher asset and must be regenerated whenever the source changes.

`previews/files-portrait.png` is the 568x1232 release preview referenced by
`lpm.toml`. Preview images are publishing assets and are not installed in the
Debian package.
