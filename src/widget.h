#pragma once
// A small transparent strip on the taskbar, just left of the notification area, showing
// "[icon] 30%  [icon] 93%". Implemented as a layered child window of the taskbar (Shell_TrayWnd), with a
// copy on each secondary-monitor taskbar (Shell_SecondaryTrayWnd) next to its clock.
#include <windows.h>
#include <string>
#include <vector>
#include "brands.h"
#include "devices.h"

struct WidgetItem {
    DeviceKind kind = DeviceKind::Other;
    int  percent  = -1;     // -1 = unknown
    std::wstring text;      // shown instead of the percentage when set (coarse levels like "Low")
    bool charging = false;
    bool full     = false;  // on power and fully charged: a green check mark instead of the bolt
    bool dim      = false;  // asleep / stale value
    bool low      = false;  // at or below the device's low threshold: red
    bool warn     = false;  // within 15 points above it: orange
    bool bluetooth = false;  // picks the Bluetooth icon when the device type is unknown
    std::wstring label;      // tiny caption on the icon; set only when two devices of one kind are shown
    Accent accent  = Accent::None;  // brand color for the icon, its glow and the level bar

    bool operator==(const WidgetItem& other) const {
        return kind == other.kind && percent == other.percent && text == other.text &&
               charging == other.charging && full == other.full && dim == other.dim &&
               low == other.low && warn == other.warn && bluetooth == other.bluetooth &&
               label == other.label && accent == other.accent;
    }
};

namespace widget {
// Creates the widget on the primary taskbar. Clicks are reported by posting `clickMsg` to `notifyWnd`,
// wParam 0 for the left button and 1 for the right one.
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip);
void SetLightTheme(bool light);
// Re-reads the TaskbarOffset / SecondaryTaskbarOffset settings and moves the strips.
void ReloadSettings();
// Draws a rounded plate in the taskbar's colours behind the text (kept across Create/Destroy).
void SetBackground(bool on);
// Icon above the percentage instead of beside it, when the taskbar is tall enough (kept across Create/Destroy).
void SetStacked(bool on);
// Which taskbars show the strip: L"all" (default), L"main", or the id of one other monitor from
// OtherMonitors() (then only that monitor shows it). Kept across Create/Destroy.
void SetMonitorChoice(const std::wstring& choice);
struct MonitorOption {
    std::wstring id;     // lasting monitor identity, stored in the settings
    std::wstring label;  // "Display 3 (right)"
};
// The monitors other than the main one that have a horizontal taskbar (empty with one monitor).
std::vector<MonitorOption> OtherMonitors();
// Re-checks the taskbar layout and moves the widget if needed; call about once a second.
// Returns false when the taskbar it was attached to is gone.
bool Reposition();
}
