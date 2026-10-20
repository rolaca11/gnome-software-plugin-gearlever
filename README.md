# GNOME Software plugin for Gearlever

This plugin exposes AppImages managed by [Gearlever](https://github.com/mijorus/gearlever) in GNOME Software’s Installed page and shows their available updates on the Updates page. Installing an update in GNOME Software delegates the download, validation, and AppImage replacement to Gearlever, so it uses the same update sources and configuration as the Gearlever UI.

The plugin supports both a native `gearlever` executable and the Flathub installation (`it.mijorus.gearlever`). Native Gearlever is preferred when both are present.

## Requirements

- GNOME Software 49 or 50 (plugin ABI 23)
- Gearlever with `--list-updates --json` support (older releases continue to work through a legacy-output fallback)
- Meson, Ninja, a C compiler, GLib/GIO development files, and JSON-GLib development files
- GNOME Software’s private `libgnomesoftware.so.23` runtime library

Ubuntu installs that private library with the `gnome-software` package, even though it does not publish development headers. This project carries only the minimal ABI 23 declarations needed by the plugin.

On Ubuntu, the JSON dependency is provided by `libjson-glib-dev`.

## Build and install

```sh
meson setup build --prefix=/usr
meson compile -C build
meson test -C build
sudo meson install -C build
```

The project defaults to `/usr` because GNOME Software does not scan a parallel
`/usr/local` plugin directory. If an existing build directory was originally
configured with the Meson default prefix, correct it before installing:

```sh
meson configure build --prefix=/usr
```

Restart GNOME Software after installation:

```sh
gnome-software --quit
```

The module is installed to `${libdir}/gnome-software/plugins-23/`. On a distribution using a different private library location, pass it explicitly:

```sh
meson setup build --prefix=/usr \
  -Dgnome_software_libdir=/usr/lib/x86_64-linux-gnu/gnome-software
```

## Behavior and limitations

- Update checks are performed asynchronously through `gearlever --list-updates --json` (or its Flatpak equivalent). The versioned schema supplies stable IDs, versions, sizes, update managers, and paths without scraping human-readable output.
- Installed AppImages are loaded asynchronously through `gearlever --list-installed --json` and enriched from their Gearlever-generated desktop entries.
- If Gearlever does not support JSON output yet, the plugin automatically falls back to its legacy table parser.
- Updates run through `gearlever --update PATH --yes`; Gearlever remains responsible for update-source support and file replacement.
- Automatic updates remain enabled. Completion requires Gearlever to finish successfully and replace the AppImage. Skipped, failed, and incomplete background updates are excluded from completion notifications.
- Running AppImages are not force-updated. Close the application and retry when GNOME Software reports that it is running.
- Gearlever currently combines download and installation. GNOME Software’s background download-only phase is therefore acknowledged without downloading; the actual download happens when the update is applied.
- AppImage integration and removal remain in Gearlever.

## Compatibility

GNOME Software’s plugin API is private. The default build targets ABI 23, used by GNOME Software 49/50. A future GNOME Software ABI change will require updating `include/gs-api-23.h` and the install directory before this plugin can be loaded safely.

For development, an uninstalled Gearlever build can be selected inside the installed Gearlever Flatpak runtime by setting `GEARLEVER_FLATPAK_EXECUTABLE` to its absolute executable path.
