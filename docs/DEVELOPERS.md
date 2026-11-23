# For developers

[README](../README.md) · [CONTRIBUTING](../CONTRIBUTING.md) · [Device table](DEVICES.md) · [Plan](PLAN.md) · [Design](DESIGN.md) · [Decisions](adr)

## How it works

Every vendor driver in [`src/drivers/`](../src/drivers) recognises its devices by USB vendor and product ID and only
sends read-only battery queries. Unknown devices are never written to. Vendor drivers run first and claim what
they handle; the generic fallbacks (HID battery usage, Bluetooth, XInput) only see unclaimed devices. Vendors
whose ids no other driver uses run in parallel lanes ([ADR 0007](adr/0007-parallel-poll-lanes.md)).

The taskbar widget is a layered child window of `Shell_TrayWnd`, placed left of `TrayNotifyWnd`, and is
re-created when Explorer restarts.

The overview, settings and history windows draw themselves in one palette (`src/ui_theme.h`, [DESIGN.md](DESIGN.md)).

### Project map

| Path | Responsibility |
|---|---|
| [`src/main.cpp`](../src/main.cpp) | Windows entry point, command line, tray entries, menu, poll loop |
| [`src/devices.cpp`](../src/devices.cpp) | Driver chain and parallel lanes, backoff, `--timing`, applies `IgnoreDevices` |
| [`src/driver_lanes.h`](../src/driver_lanes.h), [`src/driver_backoff.h`](../src/driver_backoff.h) | Which vendors poll in parallel; how silent drivers are skipped |
| [`src/drivers/`](../src/drivers) | One file per vendor protocol; `drivers.h` declares them and `PollContext` |
| [`src/drivers/*_protocol.h`](../src/drivers) | Pure frame parsers, shared by drivers and tests |
| [`src/hid.cpp`](../src/hid.cpp) | Win32 HID layer: cached enumeration, `HidDev` open / read / write |
| [`src/hid_util.cpp`](../src/hid_util.cpp) | Portable helpers: `GuessKind`, level mapping, `Hex4` |
| [`src/ui_theme.cpp`](../src/ui_theme.cpp) | The palette, theme rules, fonts and glyphs ([DESIGN.md](DESIGN.md)) |
| [`src/status_text.h`](../src/status_text.h) | Portable display rules: charging, full, red / orange, status words |
| [`src/tray_icons.cpp`](../src/tray_icons.cpp) | Tray battery icons |
| [`src/widget.cpp`](../src/widget.cpp) | Taskbar widget: layout, painting, monitors, theme |
| [`src/overview.cpp`](../src/overview.cpp), [`src/overview_cards.cpp`](../src/overview_cards.cpp) | Device overview window and its card list |
| [`src/settings_window.cpp`](../src/settings_window.cpp), [`src/settings_rows.cpp`](../src/settings_rows.cpp) | Settings window and its rows |
| [`src/device_report.cpp`](../src/device_report.cpp), [`src/report_text.h`](../src/report_text.h) | "Report this device": a prefilled GitHub issue |
| [`src/console_output.cpp`](../src/console_output.cpp) | `--list`, `--json`, `--dump`, `--timing` output |
| [`src/demo_devices.cpp`](../src/demo_devices.cpp) | Sample devices for `--demo` |
| [`src/status_file.cpp`](../src/status_file.cpp) | `status.json` for other apps |
| [`src/power_supply.h`](../src/power_supply.h) | Linux kernel peripheral batteries (`/sys/class/power_supply`) |
| [`src/brands.cpp`](../src/brands.cpp) | Brand colors and brand detection |
| [`src/battery_history.cpp`](../src/battery_history.cpp) | History file and chart viewer |
| [`src/history_model.h`](../src/history_model.h), [`src/runtime_estimate.h`](../src/runtime_estimate.h) | Portable history policy and time-to-empty estimate |
| [`src/alert_rules.h`](../src/alert_rules.h) | Portable low-battery alert policy |
| [`src/ignore_list.h`](../src/ignore_list.h) | Portable `IgnoreDevices` parser and result filter, shared by the poll and the settings window |
| [`src/device_aliases.h`](../src/device_aliases.h), [`src/rename_dialog.cpp`](../src/rename_dialog.cpp) | Custom device names |
| [`src/labels.cpp`](../src/labels.cpp) | Short captions for devices of the same kind |
| [`src/i18n.cpp`](../src/i18n.cpp) | Translations, `T()` and `TF()` |
| [`src/update.cpp`](../src/update.cpp), [`src/update_core.h`](../src/update_core.h) | Update check, download and signature verification |
| [`src/self_update.cpp`](../src/self_update.cpp) | A portable copy replaces its own EXE and restarts ([ADR 0008](adr/0008-portable-self-update.md)) |
| [`src/json_output.h`](../src/json_output.h) | `--json` output, shared by Windows and Linux |
| [`src/diagnostics.cpp`](../src/diagnostics.cpp) | Extra `--dump` sections (USB, Bluetooth) |
| [`src/app_settings.cpp`](../src/app_settings.cpp) | Registry settings under `HKCU\Software\2.4BatteryMonitor` |
| [`src/platform_compat.h`](../src/platform_compat.h) | Lets the shared drivers compile on Linux |
| [`src/linux/`](../src/linux) | Linux entry point, hidraw backend, driver list |
| [`tests/`](../tests) | One test executable per driver or module, no framework; `fake_hid.h` simulates `HidDev` |
| [`.github/ISSUE_TEMPLATE/`](../.github/ISSUE_TEMPLATE) | Issue forms: device report, new device |
| [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) | CI: `make check` on Windows, the Linux build and its tests on Ubuntu |
| [`lang/`](../lang) | UI languages, built into the EXE as a resource |
| [`res/`](../res) | Icon, manifest, resource script |
| [`installer/`](../installer) | Inno Setup script, packaging, update repository name |
| [`scripts/`](../scripts) | Version header, release signing, version check |
| [`linux/`](../linux) | Linux makefile, udev rule, desktop file |
| [`docs/`](./) | [Device table](DEVICES.md), per-vendor protocol notes, `images/` for the README (screenshots and animated SVGs) |
| `README.md`, `README.en.md`, `README.zh-CN.md` | README for visitors in Russian (default), English and Chinese |

### Where to look

| Task | Start here |
|---|---|
| Add a device to an existing vendor | `src/drivers/<vendor>.cpp`, its product table |
| Add a new vendor | [CONTRIBUTING.md](../CONTRIBUTING.md#adding-a-device), `src/drivers/drivers.h`, `src/devices.cpp#PollAllDevices` |
| Driver helper API | `src/drivers/drivers.h#PollContext`, `src/hid.h#HidDev` |
| Command-line flags | `src/main.cpp#wWinMain` |
| Widget look and layout | `src/widget.cpp#PaintStrip` |
| Brand color or brand rule | `src/brands.cpp#kBrands`, `src/brands.cpp#BrandOf` |
| Alert thresholds, charge reminder | `src/alert_rules.h#ShouldNotify`, `#ShouldNotifyCharged` |
| Colors, theme, fonts | `src/ui_theme.h#Palette` |
| How a level is shown (red, charging) | `src/status_text.h#ShownState`, `#LevelOf` |
| Settings window rows | `src/settings_rows.cpp#SettingsRows` |
| Ignored devices (`IgnoreDevices`) | `src/ignore_list.h#Parse`, `src/devices.cpp#FilterIgnored`, `src/drivers/generic.cpp#PollBluetooth`, `src/ignore_list.h#DropIgnored` |
| Parallel lanes, slow drivers | `src/driver_lanes.h#kLanes`, `src/devices.cpp#PollAllDevices`, `--timing` |
| Driver test without hardware | `tests/fake_hid.h`, e.g. `tests/kysona_driver_test.cpp` |
| Time-to-empty estimate | `src/runtime_estimate.h#EstimateRuntime` |
| New UI text | `src/i18n.h#TF`, then `lang/_template.txt` |
| JSON fields | `src/json_output.h#DevicesToJson` |
| Update check and signature | `src/update_core.h`, `scripts/sign-update.ps1` |
| Update notification, portable update | `src/update.cpp#InstallAvailable`, `src/self_update.cpp`, `--updated-from` in `src/main.cpp#wWinMain` |
| Linux driver list | `linux/Makefile.linux#DRIVERS`, `src/linux/devices_linux.cpp#PollAllDevices` |

Contributions are welcome. Read [CONTRIBUTING.md](../CONTRIBUTING.md) first: drivers must stay read-only, match
devices exactly and always time out.

## Build from source

Needs MinGW-w64 (for example from [MSYS2](https://www.msys2.org/): `pacman -S mingw-w64-x86_64-gcc make`)
and PowerShell. Use `make` from an MSYS2 MinGW-w64 shell, or `build.bat` with `g++` and `windres` on `PATH`.

```text
make                     # local build, version 0.0.0
make VERSION=1.0.0       # versioned build
build.bat 1.0.0          # the same from cmd.exe
make test                # regression tests
make check               # build + all tests + icon check
```

With Inno Setup installed, `powershell -File installer\build-installer.ps1 -Version 1.0.0` builds the installer
and the portable ZIP from the matching executable.

Tests: one executable per driver or module, no framework. `make build/tests/<name>.exe`, then
`./build/tests/<name>.exe` from the repository root runs a single one. Driver tests use the scripted `HidDev` of
[`tests/fake_hid.h`](../tests/fake_hid.h). `make check` builds with `-Werror`.

## Linux build

The Linux build uses the same vendor drivers with a hidraw HID backend and a GTK4 StatusNotifierItem tray icon.
Linux-only files are in `linux/` and `src/linux/`.

| | Fedora | Ubuntu / Debian |
|---|---|---|
| Compiler and make | `sudo dnf install gcc-c++ make` | `sudo apt install g++ make` |
| GTK4 + Cairo | `sudo dnf install gtk4-devel cairo-devel` | `sudo apt install libgtk-4-dev libcairo2-dev` |

```sh
./build-linux.sh               # build ./2.4BatteryMonitor
./build-linux.sh test          # HID descriptor parser, JSON, status text and kernel battery tests
./2.4BatteryMonitor --list
```

HID access needs a udev rule, otherwise the app finds no devices:

```sh
sudo cp linux/99-24battery-monitor.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# then log out and back in
```

KDE Plasma shows the tray icon natively; GNOME needs an AppIndicator host such as
`gnome-shell-extension-appindicator`. The optional launcher is `linux/2.4BatteryMonitor.desktop` (install it with
`linux/2.4BatteryMonitor.svg` and the binary into `~/.local/`).

Phase 1 covers Logitech, Razer, SteelSeries, Corsair, HyperX, PlayStation, Cooler Master, Glorious, Finalmouse and
the shared Kysona / Pulsar / ATK / G-Wolves protocols. Phase 2 adds every peripheral battery the kernel already reads
(`/sys/class/power_supply`, scope *Device*: Bluetooth HID, hid-logitech-hidpp, hid-sony, hid-nintendo, hid-steam and
others) for devices those drivers did not find. The other vendor drivers are planned.

## Publishing a release (maintainer)

The repository for update checks is set in `installer/update-repository.txt` as `OWNER/REPO`.

1. Once, run `powershell -File scripts\sign-update.ps1 -InitializeKey` on the maintainer's Windows account.
   Keep access to that account's signing key. Commit only `installer/update-public-key.txt`.
2. Build and package matching versions: `build.bat 1.2.3`, then
   `powershell -File installer\build-installer.ps1 -Version 1.2.3`.
3. Run `powershell -File scripts\sign-update.ps1 -Version 1.2.3`: it signs the installer and
   `2.4BatteryMonitor-1.2.3.exe`. Attach both files, both `.sig`, the portable ZIP and `SHA256SUMS.txt` to public
   release `v1.2.3` and mark it as the latest stable release. Installed copies update from the installer, portable
   ones from the EXE ([ADR 0008](adr/0008-portable-self-update.md)).

The ECDSA P-256 signature binds the installer SHA-256 to its name and repository. A new signing key requires
users to upgrade manually to a build with the new public key.
