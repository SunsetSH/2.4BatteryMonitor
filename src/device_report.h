#pragma once
// "Report this device..." / "Report a missing device...": opens a new GitHub issue in the browser with
// the app version, Windows version, the device's identity and its `--dump` lines filled in. The app
// sends nothing itself; the user sees the text on GitHub and decides whether to submit it.
#include <windows.h>

#include "devices.h"

namespace device_report {

// `device` null: a report for a device that is not listed (every HID collection).
void Open(HWND owner, const DeviceStatus* device);

}  // namespace device_report
