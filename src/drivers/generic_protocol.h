#pragma once
// Pure parts of the generic fallbacks (generic.cpp): reading a HID battery value, the Bluetooth
// address in a device id, device types from Bluetooth class / category and the Fast Pair split.
// Portable and tested in tests/generic_protocol_test.cpp.
#include <cstdint>
#include <cwchar>
#include <string>

#include "../devices.h"

namespace generic {

// HID usages that carry a battery level: Generic Device Controls "Battery Strength" and Battery
// System "Absolute State Of Charge".
inline bool IsBatteryUsage(unsigned page, unsigned usage) {
    return (page == 0x06 && usage == 0x20) || (page == 0x85 && usage == 0x65);
}

// A battery field's value as a percentage of its logical range, -1 when it cannot be one. Apple
// devices (Magic Trackpad / Mouse / Keyboard) report a plain percentage while declaring a 0-255
// range; Linux handles them with HID_BATTERY_QUIRK_PERCENT.
inline int BatteryPercent(unsigned short vid, unsigned long value, long logicalMin, long logicalMax) {
    if (vid == 0x05AC && value <= 100) return (int)value;
    if (logicalMax > logicalMin) {
        long pct = ((long)value - logicalMin) * 100 / (logicalMax - logicalMin);
        return pct < 0 ? 0 : pct > 100 ? 100 : (int)pct;
    }
    return value <= 100 ? (int)value : -1;
}

// Bluetooth address from a main device node id ("BTHENUM\DEV_6CFBED129847\..." or "BTHLE\DEV_..."), 0 if none.
inline uint64_t BtAddress(const wchar_t* inst) {
    const wchar_t* p = inst ? wcsstr(inst, L"\\DEV_") : nullptr;
    if (!p) return 0;
    p += 5;
    uint64_t a = 0;
    for (int i = 0; i < 12; i++) {
        wchar_t c = p[i];
        int v = c >= L'0' && c <= L'9'   ? c - L'0'
                : c >= L'A' && c <= L'F' ? c - L'A' + 10
                : c >= L'a' && c <= L'f' ? c - L'a' + 10
                                         : -1;
        if (v < 0) return 0;
        a = a << 4 | (uint64_t)v;
    }
    return a;
}

constexpr DeviceKind KindFromClassOfDevice(uint32_t cod) {
    uint32_t major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    if (major == 0x02) return DeviceKind::Phone;
    if (major == 0x04) return DeviceKind::Headset;
    if (major == 0x05) {
        if ((minor & 0x0F) == 0x01 || (minor & 0x0F) == 0x02) return DeviceKind::Gamepad;
        if ((minor >> 4) == 1) return DeviceKind::Keyboard;
        if ((minor >> 4) == 2) return DeviceKind::Mouse;
    }
    return DeviceKind::Other;
}

// Classic Bluetooth devices carry a Class of Device, but LE devices (BTHLE\DEV_...) usually do not.
// Windows still files the device under a category for Settings > Bluetooth & devices; use it.
inline DeviceKind KindFromCategory(const std::wstring& c) {
    if (c == L"Communication.Phone" || c.rfind(L"Communication.Phone.", 0) == 0) return DeviceKind::Phone;
    if (c == L"Input.Keyboard") return DeviceKind::Keyboard;
    if (c == L"Input.Mouse") return DeviceKind::Mouse;
    if (c == L"Input.Gaming") return DeviceKind::Gamepad;
    if (c.rfind(L"Communication.Headset", 0) == 0 || c == L"Audio.Headphones") return DeviceKind::Headset;
    return DeviceKind::Other;
}

inline std::wstring StripServiceSuffix(std::wstring n) {
    for (const wchar_t* suf :
         { L" Hands-Free AG", L" Hands-Free", L" Avrcp Transport", L" Stereo", L" Audio Gateway" }) {
        size_t l = wcslen(suf);
        if (n.size() > l && n.compare(n.size() - l, l, suf) == 0) return n.substr(0, n.size() - l);
    }
    return n;
}

// Left / right / case from a Fast Pair broadcast. Like the AirPods driver, the shown level is the
// lower bud that is in use (discharging); charging only when every bud that reports is charging.
inline void ApplyFastPair(DeviceStatus& s, int left, bool leftChg, int right, bool rightChg, int box) {
    int disLo = -1, chgLo = -1;
    auto take = [&](int pct, bool chg) {
        if (pct < 0) return;
        int& dst = chg ? chgLo : disLo;
        if (dst < 0 || pct < dst) dst = pct;
    };
    take(left, leftChg);
    take(right, rightChg);
    if (disLo >= 0 || chgLo >= 0) {
        s.percent = disLo >= 0 ? disLo : chgLo;
        s.charging = disLo < 0;
    }
    s.left = left;
    s.right = right;
    s.casePct = box;
}

}  // namespace generic
