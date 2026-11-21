#pragma once
#include <string>
#include <vector>

struct HidColl;

// Extra local USB, HID and Bluetooth information for --dump.
std::wstring DumpExtras(const std::vector<HidColl>& colls);
