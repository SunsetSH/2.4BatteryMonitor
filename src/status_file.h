#pragma once
// %LOCALAPPDATA%\2.4BatteryMonitor\status.json: the devices of the last poll in the `--json` format,
// for Rainmeter, Stream Deck, OBS or scripts that should not start a second process. Written only when
// the user turns it on in the settings; replaced atomically, so a reader never sees half a file.
#include <vector>

#include "devices.h"

namespace status_file {

bool Write(const std::vector<DeviceStatus>& devices);

}  // namespace status_file
