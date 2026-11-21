#pragma once
// Pure, portable policy for per-device low-battery notifications.
// No HID access, Windows dependencies, timers or disk/network writes.
#include "devices.h"

#include <map>
#include <string>

namespace alerts {
constexpr int kDefaultThreshold = 15;
constexpr int kUseDefault = -2;   // no per-device override persisted
constexpr int kDisabled = -1;     // notifications disabled for this device
constexpr int kThresholds[] = {5, 10, 15, 20, 25, 30, 35, 40};
constexpr int kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);

inline bool ValidOverride(int value) {
    if (value == kDisabled) return true;
    for (int threshold : kThresholds)
        if (value == threshold) return true;
    return false;
}

inline int OverrideFor(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    const auto it = overrides.find(key);
    return it == overrides.end() ? kUseDefault : it->second;
}

inline int EffectiveThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = OverrideFor(overrides, key);
    return value == kUseDefault ? kDefaultThreshold : value;
}

// The level at which the widget, tray icon and overview turn red: the device's own alert threshold,
// or the default one when it has none or its alerts are off.
inline int DisplayThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = OverrideFor(overrides, key);
    return value >= 0 ? value : kDefaultThreshold;
}

// Cached, unknown, sleeping and disconnected readings leave the latch alone.
inline bool ShouldNotify(const DeviceStatus& s, int threshold, bool& warned) {
    if (threshold == kDisabled) { warned = false; return false; }
    if (!s.HasFreshBattery()) return false;
    if (s.charging) { warned = false; return false; }
    if (s.percent > threshold + 10) { warned = false; return false; }
    if (s.percent <= threshold && !warned) {
        warned = true;
        return true;
    }
    return false;
}
// "Charged to N%" reminder (unplug to spare the battery): per device, off by default.
constexpr int kChargeTargets[] = {80, 90, 100};
constexpr int kChargeTargetCount = sizeof(kChargeTargets) / sizeof(kChargeTargets[0]);

inline bool ValidChargeTarget(int value) {
    for (int t : kChargeTargets)
        if (value == t) return true;
    return false;
}

// Fires once when a device on power reaches `target` (100: also when it reports full). Unplugging it,
// or the level falling 10 points below the target, re-arms the reminder. Readings that are not fresh
// leave the latch alone.
inline bool ShouldNotifyCharged(const DeviceStatus& s, int target, bool& notified) {
    if (!ValidChargeTarget(target)) { notified = false; return false; }
    if (!s.connected || s.asleep || s.cached || s.percent < 0 || s.percent > 100) return false;
    const bool onPower = s.charging || s.full;
    if (!onPower || s.percent < target - 10) { notified = false; return false; }
    if (!notified && (s.percent >= target || (target == 100 && s.full))) {
        notified = true;
        return true;
    }
    return false;
}
}  // namespace alerts
