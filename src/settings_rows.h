#pragma once
// The rows of the settings window. Switches and choices that belong to the running app (tray or
// widget, poll interval, ...) go through SettingsApi, which main.cpp fills; the rest (language,
// offsets, ignored devices, updates) is handled here directly.
#include <functional>
#include <string>
#include <vector>

#include "devices.h"
#include "settings_window.h"

struct SettingsApi {
    // Switches by registry name: TaskbarWidget, WidgetBackground, StackedLayout, BrandColors,
    // HideLastKnownDevices, ShowDisconnectedDevices, RecordBatteryHistory, StatusFile,
    // QuietInFullScreen, Autostart.
    std::function<bool(const std::wstring& name)> getSwitch;
    std::function<void(const std::wstring& name, bool on)> setSwitch;
    std::function<unsigned long()> pollSeconds;
    std::function<void(unsigned long)> setPollSeconds;
    std::function<std::wstring()> widgetMonitor;  // "all", "main" or a monitor id
    std::function<void(const std::wstring&)> setWidgetMonitor;
    std::function<void(const std::wstring& code)> setLanguage;
    std::function<std::vector<DeviceStatus>()> devices;  // the devices shown now, for "Ignored devices"
    std::function<void()> refreshDevices;
    bool demo = false;
};

std::vector<settings_window::Row> SettingsRows(const SettingsApi& api);
