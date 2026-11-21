# Keychron keyboard battery over the 2.4 GHz receiver

Keychron's stock firmware only reports the keyboard's battery over Bluetooth. With the small QMK change in
[`k5_max_battery.patch`](k5_max_battery.patch), the keyboard also answers a battery query (`A4`) through the
Keychron Link receiver and over the USB cable, and 2.4BatteryMonitor shows its level, voltage and charging state.

Tested on the **Keychron K5 Max ANSI RGB** (`3434:0A50`) with the Keychron Link receiver (`3434:D030`).

## What the patch changes

Based on Keychron/qmk_firmware branch `2025q3` (commit `57e682c`):

- adds the `KC_GET_BATTERY` (`0xA4`) raw HID command from ldkv/qmk_firmware#1:
  reply `[A4][percent][mV lo][mV hi][charging: 0 battery / 1 charging / 2 full][transport][model id]`;
- passes raw HID requests that arrive over the wireless link to the same handler;
- in `keyboards/keychron/k5_max/config.h`: `#define WILRESS_RAW_ENABLE` (spelled this way in Keychron's code) and
  `#define KC_BATTERY_MODEL_ID 0`.

Nothing else changes: keymaps, lighting and Keychron Launcher keep working.

## Build from source

From a local checkout of Keychron's QMK firmware at `57e682c` with submodules initialized,
apply the supplied patch and compile the keyboard target using a local QMK toolchain.

```sh
git apply /path/to/k5_max_battery.patch
qmk compile -kb keychron/k5_max/ansi/rgb -km keychron
```

The result is `keychron_k5_max_ansi_rgb_keychron.bin`. For other Keychron wireless keyboards
on the `2025q3` tree with `LK_WIRELESS_ENABLE`, apply the common changes and add the two
`#define` lines to the target keyboard's `config.h`.

## Flashing

1. Set the mode switch to **Cable** and unplug the USB cable.
2. Hold **Esc** while plugging the cable in. The keyboard enters the STM32 bootloader.
3. Flash the `.bin` with QMK Toolbox (or `dfu-util`).
4. Switch back to **2.4 GHz** and check with `2.4BatteryMonitor.exe --list`:
   `Keychron K5 Max  [2.4 GHz receiver]  52%, 3743 mV`.

Updating the keyboard from Keychron Launcher replaces this firmware and removes battery reporting. To undo it,
flash Keychron's official firmware the same way.

## License

QMK is GPL-2.0-or-later; the patch is under the same license. Anyone who receives a binary built from it must
also be able to get this source change.
