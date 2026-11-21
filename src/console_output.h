#pragma once
// Command-line output: `--list`, `--json`, `--dump`, `--timing`. Always English.
#include <string>
#include <vector>

#include "devices.h"

namespace console {

int List(const std::vector<DeviceStatus>& devices);
int Json(const std::vector<DeviceStatus>& devices);
// Writes the report to the console, or without one to a new temporary text file it opens.
int Report(const std::wstring& text);
// `--timing`: every driver of one poll with its lane, time and devices found, slowest first.
int Timing(const std::vector<DriverTiming>& timings, const std::vector<DeviceStatus>& devices, unsigned wallMs);

}  // namespace console
