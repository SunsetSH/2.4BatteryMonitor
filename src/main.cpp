// 2.4BatteryMonitor - tray app showing the battery level of wireless mice, headsets and Bluetooth devices.
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <shobjidl.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>
#include <wtsapi32.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "device_aliases.h"
#include "alert_rules.h"
#include "devices.h"
#include "battery_history.h"
#include "i18n.h"
#include "ignore_list.h"
#include "json_output.h"
#include "labels.h"
#include "rename_dialog.h"
#include "widget.h"
#include "update.h"
#include "app_settings.h"
#include "console_output.h"
#include "demo_devices.h"
#include "device_report.h"
#include "driver_backoff.h"
#include "overview.h"
#include "settings_rows.h"
#include "status_file.h"
#include "status_text.h"
#include "tray_icons.h"
#include "ui_theme.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT WM_OPEN_OVERVIEW = WM_APP + 4;
constexpr UINT WM_OVERVIEW_REFRESH = WM_APP + 5;
constexpr UINT WM_OVERVIEW_SETTINGS = WM_APP + 6;
constexpr UINT WM_OVERVIEW_CLOSE = WM_APP + 7;
constexpr UINT WM_OVERVIEW_HISTORY = WM_APP + 8;
constexpr UINT WM_OVERVIEW_MENU = WM_APP + 9;
constexpr UINT WM_OVERVIEW_MOVE = WM_APP + 10;
constexpr UINT WM_OVERVIEW_REPORT = WM_APP + 11;
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT_PTR TIMER_WIDGET = 2;
constexpr UINT_PTR TIMER_ALERT_ICON = 3;
constexpr UINT_PTR TIMER_TRAY_RETRY = 4;
constexpr UINT_PTR TIMER_HELD_NOTES = 5;
// "Check battery every" choices in seconds.
constexpr UINT PLACEHOLDER_ID = 1;
constexpr UINT ALERT_ICON_ID = 2;

enum MenuId : UINT {
    ID_REFRESH = 100, ID_EXIT, ID_OVERVIEW, ID_ALL_DEVICES, ID_SETTINGS,
    ID_DEVICE_FIRST = 1000
};
enum DeviceCmd : UINT {
    DEV_TOGGLE, DEV_LEFT, DEV_RIGHT, DEV_RENAME, DEV_RESET_NAME, DEV_HISTORY,
    DEV_ALERT_FIRST = 6, DEV_ALERT_DEFAULT = DEV_ALERT_FIRST + 8, DEV_ALERT_OFF, DEV_REPORT,
    DEV_CHARGE_OFF, DEV_CHARGE_FIRST, DEV_STRIDE = 32  // DEV_CHARGE_FIRST + index in alerts::kChargeTargets
};

const wchar_t* kAppName = L"2.4BatteryMonitor";
const wchar_t* kRunKey = app_settings::kRunKey;
const wchar_t* kRunValue = app_settings::kRunValue;
const wchar_t* kWindowClass = L"24BatteryMonitorWnd";

HINSTANCE g_inst;
HWND g_hwnd;
HANDLE g_refreshEvt, g_stopEvt, g_worker;
std::mutex g_resultsMutex;
std::optional<std::vector<DeviceStatus>> g_pendingResults;
bool g_pendingPartial = false;  // g_pendingResults is what a running poll found so far
unsigned g_pendingServed = 0;   // the g_refreshRequests value the poll behind g_pendingResults started after
// "Refresh" clicks so far. A poll that was already running when the user clicked does not end the
// "Checking batteries..." state; the next one, which starts after the click, does.
std::atomic<unsigned> g_refreshRequests{ 0 };
backoff::Schedule g_backoff;    // used by the poll threads only
std::atomic<bool> g_resetBackoff{false};  // set by the UI thread: poll every driver next time
// No polling while the session is locked or the display is off: nobody looks, and every poll wakes
// the mice and headsets. Unlocking or turning the display on polls right away.
std::atomic<bool> g_sessionLocked{false}, g_displayOff{false};
// GUID_CONSOLE_DISPLAY_STATE (winnt.h); MinGW's import libraries do not define it.
const GUID kConsoleDisplayState = { 0x6FE69556, 0x704A, 0x47A0, { 0x8F, 0x24, 0xC2, 0x8D, 0x93, 0x6F, 0xDA, 0x47 } };
std::atomic<bool> g_wlmouseOnly{false};
bool g_demo = false;
bool g_firstPollDone = false;
bool g_refreshing = true;
UINT g_taskbarCreatedMsg;
bool g_lightTaskbar;
bool g_wantWidget = false;     // user's choice: tray icons or taskbar widget
bool g_useWidget = false;      // what is actually shown right now
bool g_hideLastKnown = false;  // hide (last known) / asleep devices from taskbar and tray
bool g_showDisconnectedDevices = false;  // opt-in, explicit driver-reported offline devices only
bool g_recordHistory = true;  // save readings for "Battery history..." (menu option, on by default)
std::wstring g_widgetMonitor = L"all";  // which taskbars show the widget: "all", "main" or one monitor id
bool g_widgetBackground = false;  // draw a plate behind the taskbar text (transparent taskbar themes)
bool g_stacked = false;      // icon above the percentage (menu option)
bool g_brandColors = false;  // off by default; known brands get their color (Razer green, Logitech blue, ...)
bool g_statusFile = false;   // write status.json after every poll for other apps (settings window)
bool g_quietFullScreen = true;  // hold notifications while a full-screen app or presentation runs
std::atomic<DWORD> g_pollIntervalMs{ 60 * 1000 };  // set from the menu, read by the worker thread

struct TrayEntry {
    UINT uid = 0;
    DeviceStatus st;
    int lastPct = -1;        // last known percentage, kept while the device sleeps
    ULONGLONG lastSeen = 0;
    bool added = false;
    bool lowWarned = false;
    bool chargeNotified = false;  // the "charged to N%" reminder fired for this charge
    std::wstring iconSig;    // what the current icon shows, to avoid needless redraws
    std::wstring trayTip;
    HICON icon = nullptr;
};
std::map<std::wstring, TrayEntry> g_entries;
// Alert latches survive a device's transient removal; `used` orders them so the oldest goes first
// when the map is full.
struct AlertLatch {
    bool warned = false;
    ULONGLONG used = 0;
};
std::map<std::wstring, AlertLatch> g_alertLatches;
ULONGLONG g_latchClock = 0;
bool g_alertIconAdded = false;  // short-lived tray icon for widget-mode notifications
bool g_placeholder = false;
HICON g_placeholderIcon = nullptr;

// User-supplied names are presentation-only, keyed by stable DeviceStatus.key.
// Driver identity, HID polling, brand detection and device diagnostics are unchanged.
aliases::AliasMap g_aliases;

std::wstring DisplayName(const TrayEntry& e) {
    return aliases::DisplayName(g_aliases, e.st.key, e.st.name);
}

void LoadAliases() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, app_settings::kKey, L"DeviceAliases", RRF_RT_REG_MULTI_SZ,
                     nullptr, nullptr, &size) != ERROR_SUCCESS) return;
    if (size < 2 * sizeof(wchar_t) || size > 128 * 1024 || size % sizeof(wchar_t)) return;
    std::vector<wchar_t> data(size / sizeof(wchar_t), 0);
    if (RegGetValueW(HKEY_CURRENT_USER, app_settings::kKey, L"DeviceAliases", RRF_RT_REG_MULTI_SZ,
                     nullptr, data.data(), &size) == ERROR_SUCCESS)
        g_aliases = aliases::Decode(data.data(), size / sizeof(wchar_t));
}

void SaveAliases() {
    auto data = aliases::Encode(g_aliases);
    app_settings::SaveRaw(L"DeviceAliases", REG_MULTI_SZ, data.data(), (DWORD)(data.size() * sizeof(wchar_t)));
}

// ---- helpers ----------------------------------------------------------------------------------

UINT StableId(const std::wstring& key) {
    UINT h = 2166136261u;
    for (wchar_t c : key) { h ^= c; h *= 16777619u; }
    return (h & 0x7FFFFFFF) | 0x100;
}

// The key's StableId, or the next id no other entry uses: two keys with the same hash must not
// share one tray icon.
UINT FreeTrayId(const std::wstring& key) {
    UINT id = StableId(key);
    auto taken = [&](UINT uid) {
        for (const auto& [k, e] : g_entries)
            if (k != key && e.uid == uid) return true;
        return false;
    };
    while (taken(id)) id = ((id + 1) & 0x7FFFFFFF) | 0x100;
    return id;
}

std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

// Devices the user chose to hide (by key), stored as a REG_MULTI_SZ.
std::set<std::wstring> g_hidden;

void LoadHidden() {
    for (auto& k : app_settings::LoadList(L"HiddenDevices")) g_hidden.insert(k);
}

void SaveHidden() { app_settings::SaveList(L"HiddenDevices", { g_hidden.begin(), g_hidden.end() }); }

bool IsHidden(const std::wstring& key) { return g_hidden.count(key) != 0; }

// The user's left-to-right order (device keys), stored as a REG_MULTI_SZ. Devices not in it follow
// in the default order (by type, then name).
std::vector<std::wstring> g_order;

void LoadOrder() { g_order = app_settings::LoadList(L"DeviceOrder"); }

void SaveOrder() {
    if (g_order.size() > 64) g_order.resize(64);  // forget long-gone devices
    app_settings::SaveList(L"DeviceOrder", g_order);
}

bool IsEntryHidden(const TrayEntry& e) {
    if (IsHidden(e.st.key)) return true;
    // An explicitly offline device is not just a stale battery reading.
    if (g_showDisconnectedDevices && !e.st.connected) return false;
    return g_hideLastKnown && (e.st.asleep || e.st.cached || (e.st.percent < 0 && e.lastPct >= 0));
}

// A REG_MULTI_SZ with one "device-key<TAB>threshold" entry per override.
// Threshold -1 disables alerts; a missing key uses the default 15%.
std::map<std::wstring, int> g_alertOverrides;

void LoadAlertThresholds() {
    for (const auto& item : app_settings::LoadList(L"AlertThresholds")) {
        size_t sep = item.rfind(L'\t');
        if (sep == std::wstring::npos || sep == 0) continue;
        std::wstring value = item.substr(sep + 1);
        wchar_t* tail = nullptr;
        long n = std::wcstol(value.c_str(), &tail, 10);
        if (tail != value.c_str() && *tail == 0 && n >= -1 && n <= 40 && alerts::ValidOverride((int)n))
            g_alertOverrides[item.substr(0, sep)] = (int)n;
    }
}

void SaveAlertThresholds() {
    std::vector<std::wstring> items;
    for (const auto& [key, threshold] : g_alertOverrides) items.push_back(key + L"\t" + std::to_wstring(threshold));
    app_settings::SaveList(L"AlertThresholds", items);
}

// "Charged to N%" reminders: a REG_MULTI_SZ with one "device-key<TAB>target" entry per device.
std::map<std::wstring, int> g_chargeTargets;

void LoadChargeTargets() {
    for (const auto& item : app_settings::LoadList(L"ChargeReminders")) {
        size_t sep = item.rfind(L'\t');
        if (sep == std::wstring::npos || sep == 0) continue;
        const int target = _wtoi(item.c_str() + sep + 1);
        if (alerts::ValidChargeTarget(target)) g_chargeTargets[item.substr(0, sep)] = target;
    }
}

void SaveChargeTargets() {
    std::vector<std::wstring> items;
    for (const auto& [key, target] : g_chargeTargets) items.push_back(key + L"\t" + std::to_wstring(target));
    app_settings::SaveList(L"ChargeReminders", items);
}

// The previous on/off toggle "OtherTaskbars" maps to "all" / "main".
std::wstring LoadWidgetMonitor() {
    return app_settings::LoadText(L"WidgetMonitor", app_settings::LoadBool(L"OtherTaskbars", true) ? L"all" : L"main");
}
void SetWidgetMonitor(const std::wstring& v) {
    g_widgetMonitor = v;
    app_settings::SaveText(L"WidgetMonitor", v);
    widget::SetMonitorChoice(v);
}
DWORD LoadPollSeconds() {
    return std::clamp<DWORD>(app_settings::LoadDword(L"PollIntervalSeconds", g_wlmouseOnly ? 300 : 60), 30, 24 * 60 * 60);
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return false;
    const std::wstring exe = L"\"" + ExePath() + L"\"";
    for (const auto* args : {L"", L" --wlmouse", L" --background", L" --background --wlmouse"})
        if (_wcsicmp(buf, (exe + args).c_str()) == 0) return true;
    return false;
}

// False when the Run entry could not be written or removed.
bool SetAutostart(bool on) {
    if (g_demo) return true;
    HKEY k;
    LSTATUS status = on
        ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k);
    if (status == ERROR_FILE_NOT_FOUND && !on) return true;  // no Run key: nothing to remove
    if (status != ERROR_SUCCESS) return false;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\" --background";
        if (g_wlmouseOnly) v += L" --wlmouse";
        status =
            RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(k, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return status == ERROR_SUCCESS;
}

// ---- tray management --------------------------------------------------------------------------

void CopyTip(wchar_t* dst, size_t cap, const std::wstring& s) {
    wcsncpy(dst, s.c_str(), cap - 1);
    dst[cap - 1] = 0;
}

std::wstring Translated(const wchar_t* en) { return T(en); }

std::wstring StatusText(const TrayEntry& e) { return status_text::Describe(e.st, e.lastPct, Translated); }

Accent AccentFor(const DeviceStatus& s) { return g_brandColors ? BrandOf(s) : Accent::None; }

// Red follows the device's alert threshold (15% by default, also when its alerts are off).
status_text::Level ToneOf(const TrayEntry& e, const status_text::Shown& shown) {
    return status_text::LevelOf(shown, alerts::DisplayThreshold(g_alertOverrides, e.st.key));
}

bool TrayCall(DWORD msg, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"") {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    CopyTip(nid.szTip, ARRAYSIZE(nid.szTip), tip);
    if (infoTitle) {
        nid.uFlags |= NIF_INFO;
        CopyTip(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), infoTitle);
        CopyTip(nid.szInfo, ARRAYSIZE(nid.szInfo), info);
        nid.dwInfoFlags = NIIF_WARNING;
    }
    const bool ok = Shell_NotifyIconW(msg, &nid) != FALSE;
    if (ok && msg == NIM_ADD) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
    return ok;
}

void TrayDelete(UINT uid) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

HICON PlaceholderIcon() {
    if (!g_placeholderIcon) {
        g_placeholderIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (!g_placeholderIcon) {
            const COLORREF fg = g_lightTaskbar ? RGB(0, 0, 0) : RGB(255, 255, 255);
            g_placeholderIcon = tray_icons::Render(L"-", -1, fg, fg, true, g_lightTaskbar);
        }
    }
    return g_placeholderIcon;
}

// Notifications held while a full-screen app runs, by device key (the latest one per device).
std::map<std::wstring, std::pair<std::wstring, std::wstring>> g_heldNotes;

bool FullScreenBusy() {
    QUERY_USER_NOTIFICATION_STATE state;
    if (FAILED(SHQueryUserNotificationState(&state))) return false;
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}

void ShowNotification(TrayEntry& e, const std::wstring& title, const std::wstring& message) {
    if (g_quietFullScreen && FullScreenBusy()) {
        g_heldNotes[e.st.key] = { title, message };
        SetTimer(g_hwnd, TIMER_HELD_NOTES, 30 * 1000, nullptr);
        return;
    }
    if (!g_useWidget && e.added) {
        TrayCall(NIM_MODIFY, e.uid, e.icon, DisplayName(e) + L"\n" + StatusText(e), title.c_str(), message);
        return;
    }
    // Windows notification balloons need a tray icon. Keep a temporary one in widget mode
    // long enough for the shell to display the notification, then remove it.
    TrayCall(g_alertIconAdded ? NIM_MODIFY : NIM_ADD, ALERT_ICON_ID, PlaceholderIcon(),
             L"2.4BatteryMonitor", title.c_str(), message);
    g_alertIconAdded = true;
    SetTimer(g_hwnd, TIMER_ALERT_ICON, 60 * 1000, nullptr);
}

void ShowBatteryAlert(TrayEntry& e) {
    ShowNotification(e, T(L"Low battery"),
                     TF(L"{name} is at {percent}%", { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
}

void ShowChargedReminder(TrayEntry& e) {
    ShowNotification(e, T(L"Charged"),
                     TF(L"{name} is charged to {percent}%. You can unplug it.",
                        { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } }));
}

// Shows the held notifications once no full-screen app runs any more.
void ShowHeldNotes() {
    if (FullScreenBusy()) return;
    KillTimer(g_hwnd, TIMER_HELD_NOTES);
    auto held = std::move(g_heldNotes);
    g_heldNotes.clear();
    for (auto& [key, note] : held) {
        auto it = g_entries.find(key);
        if (it != g_entries.end()) ShowNotification(it->second, note.first, note.second);
    }
}

void UpdatePlaceholder() {
    if (g_placeholder) return;
    g_placeholder = TrayCall(NIM_ADD, PLACEHOLDER_ID, PlaceholderIcon(),
                             std::wstring(kAppName) + L"\n" + T(L"Open device overview"));
    if (g_placeholder) KillTimer(g_hwnd, TIMER_TRAY_RETRY);
    else SetTimer(g_hwnd, TIMER_TRAY_RETRY, 1500, nullptr);
}

void ApplyEntry(TrayEntry& e) {
    const auto& s = e.st;
    bool visible = !g_useWidget && !IsEntryHidden(e) && (s.connected || g_showDisconnectedDevices);
    if (!visible) {
        if (e.added) { TrayDelete(e.uid); e.added = false; }
        return;
    }
    // Never display an old percentage as a live reading for an explicitly offline device.
    const status_text::Shown shown = status_text::ShownState(s, e.lastPct);
    const status_text::Level tone = ToneOf(e, shown);
    Accent accent = AccentFor(s);
    std::wstring sig = std::to_wstring(shown.percent) + (shown.charging ? L"c" : L"") + (shown.dim ? L"d" : L"") +
                       (g_lightTaskbar ? L"l" : L"k") + L"a" + std::to_wstring((int)accent) + L"t" +
                       std::to_wstring((int)tone) + (shown.offline ? L"|off" : L"|on");
    const bool iconChanged = sig != e.iconSig || !e.icon;
    if (iconChanged) {
        if (e.icon) DestroyIcon(e.icon);
        e.icon = tray_icons::ForDevice(shown, tone, accent, g_lightTaskbar);
        e.iconSig = sig;
    }
    std::wstring tip = DisplayName(e) + L"\n" + StatusText(e);

    if (e.added && !iconChanged && tip == e.trayTip) return;
    e.added = TrayCall(e.added ? NIM_MODIFY : NIM_ADD, e.uid, e.icon, tip);
    if (e.added) e.trayTip = std::move(tip);
}

int KindOrder(DeviceKind k) {  // default order: headset first, then mouse, ...
    switch (k) {
        case DeviceKind::Headset:  return 0;
        case DeviceKind::Mouse:    return 1;
        case DeviceKind::Keyboard: return 2;
        case DeviceKind::Gamepad:  return 3;
        case DeviceKind::Phone:    return 4;
        default:                   return 5;
    }
}

bool IsShown(const TrayEntry& e) { return (e.st.connected || g_showDisconnectedDevices) && !IsEntryHidden(e); }

// Entries in left-to-right order: the user's order first, then the rest by type and name. Works the
// same for every kind of device (receivers, Bluetooth, controllers) as it only uses
// the device key.
std::vector<TrayEntry*> OrderedEntries(bool shownOnly) {
    std::vector<TrayEntry*> list;
    for (auto& [k, e] : g_entries)
        if (!shownOnly || IsShown(e)) list.push_back(&e);
    auto rank = [](const TrayEntry* e) {
        auto it = std::find(g_order.begin(), g_order.end(), e->st.key);
        return it == g_order.end() ? INT_MAX : (int)(it - g_order.begin());
    };
    std::stable_sort(list.begin(), list.end(), [&](const TrayEntry* a, const TrayEntry* b) {
        int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        int ka = KindOrder(a->st.kind), kb = KindOrder(b->st.kind);
        if (ka != kb) return ka < kb;
        return DisplayName(*a) != DisplayName(*b) ? DisplayName(*a) < DisplayName(*b) : a->st.key < b->st.key;  // same name: keep a fixed order
    });
    return list;
}

// One overview card: level, state words, earbud levels, connection and the time-to-empty estimate.
overview::Row OverviewRow(const TrayEntry& e) {
    const DeviceStatus& s = e.st;
    const status_text::Shown shown = status_text::ShownState(s, e.lastPct);
    overview::Row r;
    r.key = s.key;
    r.name = DisplayName(e);
    r.kind = s.kind;
    r.bluetooth = s.via == L"Bluetooth";
    r.percent = shown.percent;
    r.charging = shown.charging;
    r.full = shown.full;
    r.dim = shown.dim;
    r.tone = ToneOf(e, shown);
    if (shown.offline) r.level = L"\u00D7";
    else if (!s.levelText.empty() && s.percent >= 0) r.level = T(s.levelText);
    else r.level = shown.percent >= 0 ? std::to_wstring(shown.percent) + L"%" : L"?";
    std::vector<std::wstring> parts{ s.via.empty() ? std::wstring(T(L"unknown")) : T(s.via) };
    if (shown.offline) parts.push_back(T(L"off / not connected"));
    else if (shown.full) parts.push_back(T(L"fully charged"));
    else if (shown.charging) parts.push_back(T(L"charging"));
    if (s.asleep) parts.push_back(T(L"asleep"));
    else if (!shown.offline && (s.cached || (s.percent < 0 && e.lastPct >= 0))) parts.push_back(T(L"last known"));
    std::wstring buds = status_text::BudsText(s, Translated);
    if (!buds.empty()) parts.push_back(buds);
    else if (!s.detail.empty()) parts.push_back(s.detail);
    if (!shown.offline && !shown.charging && !shown.full) {
        const double hours = battery_history::HoursRemaining(s.key);
        if (hours >= 0) {
            const int total = (int)(hours + 0.5), days = total / 24;
            std::wstring time = days ? TF(L"~{days} d {hours} h", { { L"days", std::to_wstring(days) }, { L"hours", std::to_wstring(total % 24) } })
                                     : TF(L"~{hours} h", { { L"hours", std::to_wstring(total) } });
            parts.push_back(TF(L"{time} left", { { L"time", time } }));
        }
    }
    if (IsHidden(s.key)) parts.push_back(T(L"hidden"));
    for (size_t i = 0; i < parts.size(); i++) r.detail += (i ? L" \u00B7 " : L"") + parts[i];
    return r;
}

std::vector<TrayEntry*> g_overviewOrder;  // the entries in the overview's card order

SYSTEMTIME g_lastPollTime{};  // local time of the last full result; wYear 0 = none yet

void UpdateOverview() {
    if (!overview::Window()) return;
    std::vector<overview::Row> rows;
    g_overviewOrder = OrderedEntries(false);
    for (const auto* entry : g_overviewOrder) rows.push_back(OverviewRow(*entry));
    const auto* scope = g_demo ? L"Demo devices" : g_wlmouseOnly ? L"WLMouse only" : L"All supported devices";
    std::wstring status = T(scope);
    status += L"\n";
    const bool busy = g_refreshing || !g_firstPollDone;
    if (busy) status += T(L"Checking batteries...");
    else if (rows.empty()) status += T(L"No devices with battery info found");
    else status += TF(L"Detected devices: {count}", {{L"count", std::to_wstring(rows.size())}});
    if (!busy && g_lastPollTime.wYear) {
        // No polls while the session is locked: after a night the last one is from yesterday, so say so.
        SYSTEMTIME now;
        GetLocalTime(&now);
        std::wstring when;
        wchar_t buf[64] = {};
        const SYSTEMTIME& t = g_lastPollTime;
        const bool today = now.wYear == t.wYear && now.wMonth == t.wMonth && now.wDay == t.wDay;
        if (!today &&
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &t, nullptr, buf, ARRAYSIZE(buf), nullptr))
            when = std::wstring(buf) + L" ";
        if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &g_lastPollTime, nullptr, buf, ARRAYSIZE(buf)))
            status += L"  \u00B7  " + TF(L"updated {time}", { { L"time", when + buf } });
    }
    overview::Update(rows, status, busy);
}

void OpenOverview(const POINT* anchor = nullptr) {
    const overview::Messages messages{ WM_OVERVIEW_REFRESH, WM_OVERVIEW_SETTINGS, WM_OVERVIEW_CLOSE, WM_OVERVIEW_HISTORY,
                                       WM_OVERVIEW_MENU,    WM_OVERVIEW_MOVE,     WM_OVERVIEW_REPORT };
    if (overview::Open(g_inst, g_hwnd, messages, anchor))
        UpdateOverview();
    else
        MessageBoxW(nullptr, T(L"Could not open the device overview."), kAppName, MB_OK | MB_ICONERROR);
}

// Opens the overview like a flyout above the point clicked on the taskbar.
void OpenOverviewAtCursor() {
    POINT pt;
    GetCursorPos(&pt);
    OpenOverview(&pt);
}

void RefreshNow() {
    g_refreshRequests++;
    g_refreshing = true;
    g_resetBackoff = true;
    SetEvent(g_refreshEvt);
    UpdateOverview();
}

void MonitorAllDevices() {
    if (!g_wlmouseOnly.exchange(false)) return;
    // The Run entry and the restart after an update still say --wlmouse; the chosen mode must survive both.
    if (!g_demo) updates::SetWlmouseOnly(false);
    if (AutostartEnabled() && !SetAutostart(true))
        MessageBoxW(nullptr, T(L"Could not change the Windows startup entry."), kAppName, MB_OK | MB_ICONERROR);
    g_pollIntervalMs = LoadPollSeconds() * 1000;
    RefreshNow();
}

void UpdateWidget() {
    if (!g_useWidget) return;
    std::vector<TrayEntry*> list = OrderedEntries(true);
    // Two devices of one kind share an icon: give each a tiny caption so they can be told apart.
    std::vector<LabelInput> labelIn;
    for (auto* e : list) labelIn.push_back({ e->st.kind, DisplayName(*e) });
    std::vector<std::wstring> labels = ShortLabels(labelIn, 3);
    std::vector<WidgetItem> items;
    std::wstring tip;
    for (auto* e : list) {
        WidgetItem it;
        it.kind = e->st.kind;
        const status_text::Shown shown = status_text::ShownState(e->st, e->lastPct);
        it.percent = shown.percent;
        if (shown.offline) it.text = L"\u00D7";  // clear status, not a stale percentage
        else if (e->st.percent >= 0 && !e->st.levelText.empty()) it.text = T(e->st.levelText);
        it.dim = shown.dim;
        it.charging = shown.charging;
        it.full = shown.full;
        const status_text::Level tone = ToneOf(*e, shown);
        it.low = tone == status_text::Level::Low;
        it.warn = tone == status_text::Level::Warn;
        it.bluetooth = e->st.via == L"Bluetooth";
        it.label = labels[items.size()];
        it.accent = AccentFor(e->st);
        items.push_back(it);
        if (!tip.empty()) tip += L"\n";
        tip += DisplayName(*e) + L": " + StatusText(*e);
    }
    if (tip.empty()) tip = std::wstring(L"2.4BatteryMonitor: ") + T(L"No devices with battery info found");
    widget::SetItems(items, tip);
}

// `partial`: what a running poll found so far. It updates those devices only; devices it does not
// list yet are kept until the full result arrives.
// `served`: the g_refreshRequests value the poll started after.
void OnResults(std::vector<DeviceStatus>& results, bool partial, unsigned served) {
    if (!partial) {
        g_firstPollDone = true;
        if (served == g_refreshRequests) g_refreshing = false;
        GetLocalTime(&g_lastPollTime);
    }
    std::map<std::wstring, bool> seen;
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        if (!e.uid) e.uid = FreeTrayId(s.key);
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        e.lastSeen = GetTickCount64();
        seen[s.key] = true;
        // Evaluate even in widget mode: ApplyEntry only manages tray icons.
        // Alert latches are kept by stable device key even if the driver briefly stops reporting it.
        if (const auto it = g_alertLatches.find(s.key); it != g_alertLatches.end())
            e.lowWarned = it->second.warned;
        bool notify = !IsHidden(s.key) &&
                      alerts::ShouldNotify(s, alerts::EffectiveThreshold(g_alertOverrides, s.key), e.lowWarned);
        g_alertLatches[s.key] = { e.lowWarned, ++g_latchClock };
        if (g_alertLatches.size() > 256)
            g_alertLatches.erase(std::min_element(g_alertLatches.begin(), g_alertLatches.end(),
                                                  [](const auto& a, const auto& b) { return a.second.used < b.second.used; }));
        const auto target = g_chargeTargets.find(s.key);
        const bool charged = target != g_chargeTargets.end() && !IsHidden(s.key) &&
                             alerts::ShouldNotifyCharged(s, target->second, e.chargeNotified);
        if (g_recordHistory && s.HasFreshBattery())
            battery_history::Observe(s.key, s.percent, s.charging);
        ApplyEntry(e);
        if (notify) ShowBatteryAlert(e);
        if (charged) ShowChargedReminder(e);
    }
    // A device the user just ignored leaves at once instead of lingering as "asleep" for its grace time.
    const auto ignored =
        partial ? std::vector<ignore_list::Id>{} : ignore_list::Parse(app_settings::LoadText(L"IgnoreDevices"));
    for (auto it = g_entries.begin(); !partial && it != g_entries.end();) {
        auto& e = it->second;
        // Some receivers drop sleeping devices from the link; keep showing the last value for a while
        // (at least two poll intervals).
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected &&
                       !(e.st.vid && ignore_list::Contains(ignored, e.st.vid, e.st.pid)) &&
                       GetTickCount64() - e.lastSeen <
                           std::max<ULONGLONG>((ULONGLONG)e.st.graceSec * 1000, 2ULL * g_pollIntervalMs);
        if (inGrace) {
            e.st.percent = -1;
            e.st.asleep = true;
            ApplyEntry(e);
        }
        if (!seen.count(it->first) && !inGrace) {
            if (it->second.added) TrayDelete(it->second.uid);
            if (it->second.icon) DestroyIcon(it->second.icon);
            it = g_entries.erase(it);
        } else {
            ++it;
        }
    }
    UpdatePlaceholder();
    UpdateWidget();
    UpdateOverview();
    if (!partial && g_statusFile && !g_demo) status_file::Write(results);
}

// Makes the actual display match the chosen mode; falls back to tray icons if the taskbar
// widget cannot be created (e.g. Explorer not ready yet). Called once a second.
void Reconcile() {
    bool alive = g_wantWidget && widget::Alive();
    if (g_wantWidget && !alive) alive = widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
    if (!g_wantWidget) widget::Destroy();
    bool changed = alive != g_useWidget;
    g_useWidget = alive;
    if (changed) {
        for (auto& [k, e] : g_entries) ApplyEntry(e);
        UpdatePlaceholder();
        UpdateWidget();
    } else if (g_useWidget) {
        widget::Reposition();
    }
}

void SetMode(bool useWidget) {
    g_wantWidget = useWidget;
    app_settings::SaveBool(L"TaskbarWidget", useWidget);
    Reconcile();
    if (useWidget) SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    else KillTimer(g_hwnd, TIMER_WIDGET);
}

void SetWidgetBackground(bool on) {
    g_widgetBackground = on;
    app_settings::SaveBool(L"WidgetBackground", on);
    widget::SetBackground(on);
}

void SetHideLastKnown(bool on) {
    g_hideLastKnown = on;
    app_settings::SaveBool(L"HideLastKnownDevices", on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
}

void SetShowDisconnectedDevices(bool on) {
    g_showDisconnectedDevices = on;
    app_settings::SaveBool(L"ShowDisconnectedDevices", on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
}

void ReAddAll() {
    g_placeholder = false;
    for (auto& [k, e] : g_entries) { e.added = false; ApplyEntry(e); }
    UpdatePlaceholder();
}

// Switches the UI language ("" = follow Windows) and redraws everything that shows text.
void SetLanguage(const std::wstring& code);

void RedrawAll() {
    if (g_alertIconAdded) {  // alert icon shares the placeholder's HICON
        KillTimer(g_hwnd, TIMER_ALERT_ICON);
        TrayDelete(ALERT_ICON_ID);
        g_alertIconAdded = false;
    }
    if (g_placeholderIcon) {
        DestroyIcon(g_placeholderIcon);
        g_placeholderIcon = nullptr;
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
    }
    for (auto& [k, e] : g_entries) { e.iconSig.clear(); ApplyEntry(e); }
    UpdatePlaceholder();
}

void SetLanguage(const std::wstring& code) {
    i18n::SetChoice(code, !g_demo);
    RedrawAll();  // tray tooltips and the placeholder text
    UpdateWidget();
    overview::Translate();
    UpdateOverview();
}

void SetStacked(bool on) {
    g_stacked = on;
    app_settings::SaveBool(L"StackedLayout", on);
    widget::SetStacked(on);
}

void SetBrandColors(bool on) {
    g_brandColors = on;
    app_settings::SaveBool(L"BrandColors", on);
    RedrawAll();
    UpdateWidget();
}

void SetPollSeconds(DWORD seconds) {
    g_pollIntervalMs = seconds * 1000;
    app_settings::SaveDword(L"PollIntervalSeconds", seconds);
    SetEvent(g_refreshEvt);  // poll now, so the new interval starts right away
}

void ToggleHidden(const std::wstring& key) {
    if (!g_hidden.erase(key)) g_hidden.insert(key);
    SaveHidden();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdatePlaceholder();
    UpdateWidget();
}

void RenameDevice(const std::wstring& key) {
    auto it = g_entries.find(key);
    if (it == g_entries.end()) return;
    const std::wstring originalName = it->second.st.name;
    std::wstring chosen;
    if (!PromptDeviceName(g_hwnd, DisplayName(it->second), chosen)) return;
    if (chosen == originalName) g_aliases.erase(key);
    else if (g_aliases.count(key) || g_aliases.size() < aliases::kMaxAliases) g_aliases[key] = chosen;
    else return;  // bounded registry value; don't silently drop other users' names
    SaveAliases();
    // The modal editor pumps the message queue, so polling may remove the entry.
    // Resolve the iterator again rather than dereferencing a potentially stale one.
    it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdateWidget();
    UpdateOverview();
}

void ResetDeviceName(const std::wstring& key) {
    if (!g_aliases.erase(key)) return;
    SaveAliases();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdateWidget();
    UpdateOverview();
}

void SetAlertOverride(const std::wstring& key, int threshold) {
    if (threshold == alerts::kUseDefault) g_alertOverrides.erase(key);
    else if (alerts::ValidOverride(threshold)) g_alertOverrides[key] = threshold;
    else return;
    SaveAlertThresholds();
    g_alertLatches.erase(key);
    auto it = g_entries.find(key);
    if (it != g_entries.end()) it->second.lowWarned = false;
    SetEvent(g_refreshEvt);  // reconsider with a fresh reading; never alert from stale data
}

void SetChargeTarget(const std::wstring& key, int target) {
    if (alerts::ValidChargeTarget(target)) g_chargeTargets[key] = target;
    else g_chargeTargets.erase(key);
    SaveChargeTargets();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) it->second.chargeNotified = false;
}

// Swaps a shown device with its neighbour (dir -1 = left, +1 = right) and remembers the new order.
void MoveEntry(const std::wstring& key, int dir) {
    std::vector<TrayEntry*> list = OrderedEntries(true);
    auto pos = std::find_if(list.begin(), list.end(), [&](const TrayEntry* e) { return e->st.key == key; });
    if (pos == list.end()) return;
    ptrdiff_t i = pos - list.begin(), j = i + dir;
    if (j < 0 || j >= (ptrdiff_t)list.size()) return;
    std::swap(list[i], list[j]);
    std::vector<std::wstring> order;
    for (auto* e : list) order.push_back(e->st.key);
    for (auto& k : g_order)  // keep the places of devices that are not shown right now
        if (std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
    g_order = order;
    SaveOrder();
    UpdateWidget();
}

// Moves the device at `from` in the overview's card order to `to` and remembers the new order.
void MoveCard(int from, int to) {
    std::vector<TrayEntry*> list = OrderedEntries(false);
    if (from < 0 || to < 0 || from >= (int)list.size() || to >= (int)list.size() || from == to) return;
    TrayEntry* moved = list[from];
    list.erase(list.begin() + from);
    list.insert(list.begin() + to, moved);
    std::vector<std::wstring> order;
    for (auto* e : list) order.push_back(e->st.key);
    for (auto& k : g_order)  // keep the places of devices that are gone right now
        if (std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
    g_order = order;
    SaveOrder();
    UpdateWidget();
    UpdateOverview();
}

// A device's submenu: show / hide, order, name, alerts, history and the device report. Its commands
// are base + DeviceCmd.
HMENU DeviceMenu(const TrayEntry& e, UINT base, bool canLeft, bool canRight) {
    const std::wstring& k = e.st.key;
    HMENU sub = CreatePopupMenu();
    AppendMenuW(sub, MF_STRING | (IsHidden(k) ? 0 : MF_CHECKED), base + DEV_TOGGLE,
                g_wantWidget ? T(L"Show on the taskbar") : T(L"Show in the tray"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING | (canLeft ? 0 : MF_GRAYED), base + DEV_LEFT, g_wantWidget ? T(L"Move left") : T(L"Move up"));
    AppendMenuW(sub, MF_STRING | (canRight ? 0 : MF_GRAYED), base + DEV_RIGHT, g_wantWidget ? T(L"Move right") : T(L"Move down"));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING, base + DEV_RENAME, T(L"Rename device..."));
    AppendMenuW(sub, MF_STRING | (g_aliases.count(k) ? 0 : MF_GRAYED), base + DEV_RESET_NAME, T(L"Reset to original name"));
    HMENU alertMenu = CreatePopupMenu();
    int selectedThreshold = alerts::OverrideFor(g_alertOverrides, k);
    AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == alerts::kUseDefault ? MF_CHECKED : 0),
                base + DEV_ALERT_DEFAULT, T(L"Use default (15%)"));
    AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == alerts::kDisabled ? MF_CHECKED : 0),
                base + DEV_ALERT_OFF, T(L"Disable notifications"));
    AppendMenuW(alertMenu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < alerts::kThresholdCount; ++i) {
        int level = alerts::kThresholds[i];
        std::wstring label = std::to_wstring(level) + L"%";
        AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == level ? MF_CHECKED : 0), base + DEV_ALERT_FIRST + i,
                    label.c_str());
    }
    AppendMenuW(sub, MF_POPUP, (UINT_PTR)alertMenu, T(L"Battery alerts"));
    HMENU chargeMenu = CreatePopupMenu();
    const auto target = g_chargeTargets.find(k);
    AppendMenuW(chargeMenu, MF_STRING | (target == g_chargeTargets.end() ? MF_CHECKED : 0), base + DEV_CHARGE_OFF, T(L"Off"));
    for (int i = 0; i < alerts::kChargeTargetCount; ++i) {
        std::wstring label = std::to_wstring(alerts::kChargeTargets[i]) + L"%";
        bool on = target != g_chargeTargets.end() && target->second == alerts::kChargeTargets[i];
        AppendMenuW(chargeMenu, MF_STRING | (on ? MF_CHECKED : 0), base + DEV_CHARGE_FIRST + i, label.c_str());
    }
    AppendMenuW(sub, MF_POPUP, (UINT_PTR)chargeMenu, T(L"Charge reminder"));
    AppendMenuW(sub, MF_STRING, base + DEV_HISTORY, T(L"Battery history..."));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING | (g_demo ? MF_GRAYED : 0), base + DEV_REPORT, T(L"Report this device..."));
    return sub;
}

void RunDeviceCommand(const std::wstring& key, UINT slot) {
    switch (slot) {
        case DEV_TOGGLE: ToggleHidden(key); break;
        case DEV_LEFT:   MoveEntry(key, -1); break;
        case DEV_RIGHT:  MoveEntry(key, +1); break;
        case DEV_RENAME: RenameDevice(key); break;
        case DEV_RESET_NAME: ResetDeviceName(key); break;
        case DEV_ALERT_DEFAULT: SetAlertOverride(key, alerts::kUseDefault); break;
        case DEV_ALERT_OFF: SetAlertOverride(key, alerts::kDisabled); break;
        case DEV_HISTORY: {
            auto it = g_entries.find(key);
            if (it != g_entries.end()) battery_history::Show(g_hwnd, key, DisplayName(it->second));
            break;
        }
        case DEV_REPORT: {
            auto it = g_entries.find(key);
            if (it != g_entries.end() && !g_demo) {
                const DeviceStatus device = it->second.st;  // the report dialog pumps messages
                device_report::Open(overview::Window() ? overview::Window() : g_hwnd, &device);
            }
            break;
        }
        case DEV_CHARGE_OFF: SetChargeTarget(key, 0); break;
        default:
            if (slot >= DEV_ALERT_FIRST && slot < DEV_ALERT_FIRST + alerts::kThresholdCount)
                SetAlertOverride(key, alerts::kThresholds[slot - DEV_ALERT_FIRST]);
            else if (slot >= DEV_CHARGE_FIRST && slot < DEV_CHARGE_FIRST + alerts::kChargeTargetCount)
                SetChargeTarget(key, alerts::kChargeTargets[slot - DEV_CHARGE_FIRST]);
            break;
    }
}

UINT TrackMenu(HMENU m, HWND owner, POINT pt, bool light) {
    ui::ApplyMenuTheme(light);
    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | (i18n::Rtl() ? TPM_LAYOUTRTL : 0), pt.x,
                              pt.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    return cmd;
}

// The menu of one device card in the overview (right click, Shift+F10).
void ShowDeviceMenu(const std::wstring& key, POINT pt) {
    auto it = g_entries.find(key);
    HWND owner = overview::Window();
    if (it == g_entries.end() || !owner) return;
    std::vector<TrayEntry*> shown = OrderedEntries(true);
    auto pos = std::find(shown.begin(), shown.end(), &it->second);
    bool canLeft = pos != shown.end() && pos != shown.begin();
    bool canRight = pos != shown.end() && pos + 1 != shown.end();
    HMENU m = DeviceMenu(it->second, ID_DEVICE_FIRST, canLeft, canRight);
    UINT cmd = TrackMenu(m, owner, pt, ui::AppsLight());
    DestroyMenu(m);
    if (cmd >= ID_DEVICE_FIRST && cmd < ID_DEVICE_FIRST + DEV_STRIDE) RunDeviceCommand(key, cmd - ID_DEVICE_FIRST);
}

// The settings window's view of the app (settings_rows.cpp). Switches go by their registry names.
bool GetSwitch(const std::wstring& name) {
    if (name == L"TaskbarWidget") return g_wantWidget;
    if (name == L"WidgetBackground") return g_widgetBackground;
    if (name == L"StackedLayout") return g_stacked;
    if (name == L"BrandColors") return g_brandColors;
    if (name == L"HideLastKnownDevices") return g_hideLastKnown;
    if (name == L"ShowDisconnectedDevices") return g_showDisconnectedDevices;
    if (name == L"RecordBatteryHistory") return g_recordHistory;
    if (name == L"StatusFile") return g_statusFile;
    if (name == L"QuietInFullScreen") return g_quietFullScreen;
    if (name == L"Autostart") return AutostartEnabled();
    return false;
}

void SetSwitch(const std::wstring& name, bool on) {
    if (name == L"TaskbarWidget") SetMode(on);
    else if (name == L"WidgetBackground") SetWidgetBackground(on);
    else if (name == L"StackedLayout") SetStacked(on);
    else if (name == L"BrandColors") SetBrandColors(on);
    else if (name == L"HideLastKnownDevices") SetHideLastKnown(on);
    else if (name == L"ShowDisconnectedDevices") SetShowDisconnectedDevices(on);
    else if (name == L"Autostart") {
        if (!SetAutostart(on))
            MessageBoxW(nullptr, T(L"Could not change the Windows startup entry."), kAppName, MB_OK | MB_ICONERROR);
    } else {
        bool* flag = name == L"RecordBatteryHistory" ? &g_recordHistory : name == L"StatusFile" ? &g_statusFile
                   : name == L"QuietInFullScreen" ? &g_quietFullScreen : nullptr;
        if (!flag) return;
        *flag = on;
        app_settings::SaveBool(name.c_str(), on);
    }
}

void OpenSettings() {
    static SettingsApi api;  // the rows' actions refer to it while the window is open
    api.getSwitch = GetSwitch;
    api.setSwitch = SetSwitch;
    api.pollSeconds = [] { return (unsigned long)(g_pollIntervalMs / 1000); };
    api.setPollSeconds = [](unsigned long s) { SetPollSeconds(s); };
    api.widgetMonitor = [] { return g_widgetMonitor; };
    api.setWidgetMonitor = SetWidgetMonitor;
    api.setLanguage = SetLanguage;
    api.devices = [] {
        std::vector<DeviceStatus> out;
        for (const auto& [k, e] : g_entries) out.push_back(e.st);
        return out;
    };
    api.refreshDevices = RefreshNow;
    api.demo = g_demo;
    settings_window::Open(g_inst, [] { return SettingsRows(api); });
}

void ShowMenu(HWND menuOwner = nullptr) {
    if (!menuOwner) menuOwner = g_hwnd;
    HMENU m = CreatePopupMenu();
    std::vector<std::wstring> keys;  // device index -> device key; menu id ID_DEVICE_FIRST + index * DEV_STRIDE + command
    std::vector<TrayEntry*> all = OrderedEntries(false), shown = OrderedEntries(true);
    if (all.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"No devices with battery info found"));
    else AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(L"Point at a device to show, hide or move it"));
    for (TrayEntry* e : all) {
        const std::wstring& k = e->st.key;
        UINT base = ID_DEVICE_FIRST + (UINT)keys.size() * DEV_STRIDE;
        auto pos = std::find(shown.begin(), shown.end(), e);
        bool canLeft = pos != shown.end() && pos != shown.begin();
        bool canRight = pos != shown.end() && pos + 1 != shown.end();
        HMENU sub = DeviceMenu(*e, base, canLeft, canRight);
        std::wstring line = DisplayName(*e) + L":  " + StatusText(*e) + L"   [" + T(e->st.via) + L"]";
        AppendMenuW(m, MF_POPUP | (IsHidden(k) ? 0 : MF_CHECKED), (UINT_PTR)sub, line.c_str());
        keys.push_back(k);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_OVERVIEW, T(L"Open device overview"));
    if (g_wlmouseOnly) AppendMenuW(m, MF_STRING, ID_ALL_DEVICES, T(L"Monitor all supported devices"));
    AppendMenuW(m, MF_STRING, ID_SETTINGS, T(L"Settings..."));
    AppendMenuW(m, MF_STRING, ID_REFRESH, T(L"Refresh now"));
    updates::AddMenu(m);
    AppendMenuW(m, MF_STRING, ID_EXIT, T(L"Exit"));
    POINT pt;
    GetCursorPos(&pt);
    // A menu opened from the overview follows the window's theme, one from the taskbar the taskbar's.
    UINT cmd = TrackMenu(m, menuOwner, pt, menuOwner == overview::Window() ? ui::AppsLight() : g_lightTaskbar);
    DestroyMenu(m);
    if (updates::MenuCommand(cmd)) return;
    switch (cmd) {
        case ID_REFRESH: RefreshNow(); break;
        case ID_OVERVIEW: OpenOverview(); break;
        case ID_ALL_DEVICES: MonitorAllDevices(); OpenOverview(); break;
        case ID_SETTINGS: OpenSettings(); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default:
            if (cmd >= ID_DEVICE_FIRST && cmd < ID_DEVICE_FIRST + keys.size() * DEV_STRIDE) {
                RunDeviceCommand(keys[(cmd - ID_DEVICE_FIRST) / DEV_STRIDE], (cmd - ID_DEVICE_FIRST) % DEV_STRIDE);
            }
    }
}

// ---- worker thread ----------------------------------------------------------------------------

std::vector<DeviceStatus> PollDevices() {
    if (g_demo) return DemoDevices();
    return g_wlmouseOnly ? PollWlmouseDevices() : PollAllDevices();
}

void PostResults(std::vector<DeviceStatus> results, bool partial, unsigned served = 0) {
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        // A full result replaces an unread partial one; a partial one never replaces an unread full one.
        if (partial && g_pendingResults && !g_pendingPartial) return;
        g_pendingResults = std::move(results);
        g_pendingPartial = partial;
        g_pendingServed = served;
    }
    PostMessageW(g_hwnd, WM_RESULTS, 0, 0);
}

// The background poll: parallel lanes, backoff for silent drivers and results shown as they come in.
std::vector<DeviceStatus> PollInBackground() {
    if (g_demo || g_wlmouseOnly) return PollDevices();
    if (g_resetBackoff.exchange(false)) g_backoff.Reset();
    PollOptions options;
    options.waitForInitialControllers = false;
    options.schedule = &g_backoff;
    options.intervalMs = g_pollIntervalMs;
    options.partial = [](std::vector<DeviceStatus> found) { PostResults(std::move(found), true); };
    return PollAllDevices(options);
}

bool Paused() { return g_sessionLocked || g_displayOff; }

DWORD WINAPI Worker(LPVOID) {
    // The apartment stays for the thread's life: WinRT objects of the drivers (BLE watcher, Gaming
    // Input) outlive the lane threads of a single poll.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // At sign-in a USB receiver may enumerate before its wireless link is ready.
    constexpr DWORD startupPollDelays[] = { 3000, 7000, 15000 };
    size_t startupPoll = 0;
    HANDLE events[2] = { g_stopEvt, g_refreshEvt };
    for (;;) {
        while (Paused()) {  // locked or display off: wait for unlock (a refresh event) or exit
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) return 0;
        }
        const unsigned served = g_refreshRequests;
        auto results = PollInBackground();
        if (g_wlmouseOnly && std::any_of(results.begin(), results.end(), [](const DeviceStatus& s) {
                return s.connected && s.percent >= 0 && !s.asleep;
            })) startupPoll = ARRAYSIZE(startupPollDelays);
        PostResults(std::move(results), false, served);
        DWORD delay = g_pollIntervalMs;
        if (startupPoll < ARRAYSIZE(startupPollDelays)) delay = startupPollDelays[startupPoll++];
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, delay);
        if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT) return 0;
    }
}

void RefreshSoon(UINT ms) { SetTimer(g_hwnd, TIMER_DEBOUNCE, ms, nullptr); }

bool IsWlmouseDeviceChange(LPARAM data) {
    const auto* info = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(data);
    constexpr wchar_t prefix[] = L"\\\\?\\HID#VID_36A7&PID_";
    return info && info->dbcc_devicetype == DBT_DEVTYP_DEVICEINTERFACE &&
           info->dbcc_size >= offsetof(DEV_BROADCAST_DEVICEINTERFACE_W, dbcc_name) + sizeof(prefix) &&
           _wcsnicmp(info->dbcc_name, prefix, ARRAYSIZE(prefix) - 1) == 0;
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {  // Explorer restarted
        widget::Destroy();
        g_useWidget = false;
        g_alertIconAdded = false;
        KillTimer(g_hwnd, TIMER_ALERT_ICON);
        ReAddAll();
        Reconcile();
        return 0;
    }
    switch (msg) {
        case WM_RESULTS: {
            // Messages only wake the UI. Both threads exchange results in process memory.
            std::optional<std::vector<DeviceStatus>> results;
            bool partial = false;
            unsigned served = 0;
            {
                std::lock_guard<std::mutex> lock(g_resultsMutex);
                results.swap(g_pendingResults);
                partial = g_pendingPartial;
                served = g_pendingServed;
            }
            if (results) OnResults(*results, partial, served);
            return 0;
        }
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU: ShowMenu(); break;
                case WM_RBUTTONUP: if (!HIWORD(lp)) ShowMenu(); break;
                case WM_LBUTTONUP: if (!HIWORD(lp)) OpenOverviewAtCursor(); break;
                case NIN_SELECT:
                case NIN_KEYSELECT: {
                    // Enter / Space on a focused icon: version 4 puts the icon's position in wParam, and the
                    // mouse pointer can be anywhere. A click and a missing position (0) use the pointer.
                    POINT at;
                    if (LOWORD(lp) == NIN_KEYSELECT && wp)
                        at = { (SHORT)LOWORD(wp), (SHORT)HIWORD(wp) };
                    else
                        GetCursorPos(&at);
                    OpenOverview(&at);
                    break;
                }
            }
            return 0;
        case WM_OPEN_OVERVIEW:
            if (wp) MonitorAllDevices();
            OpenOverview();
            return 0;
        case WM_OVERVIEW_REFRESH: RefreshNow(); return 0;
        case WM_OVERVIEW_SETTINGS: OpenSettings(); return 0;
        case WM_OVERVIEW_HISTORY: {
            auto it = g_entries.find(overview::KeyAt((int)wp));
            if (it != g_entries.end()) battery_history::Show(g_hwnd, it->first, DisplayName(it->second));
            return 0;
        }
        case WM_OVERVIEW_MENU: {
            const std::wstring key = overview::KeyAt((int)wp);
            if (!key.empty()) ShowDeviceMenu(key, POINT{ (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) });
            return 0;
        }
        case WM_OVERVIEW_MOVE: MoveCard((int)wp, (int)lp); return 0;
        case WM_OVERVIEW_REPORT: if (!g_demo) device_report::Open(overview::Window(), nullptr); return 0;
        case WM_OVERVIEW_CLOSE:
            if (g_placeholder) ShowWindow(overview::Window(), SW_HIDE);
            else DestroyWindow(h);
            return 0;
        case WM_WIDGET_CLICK:
            if (wp) ShowMenu();
            else OpenOverviewAtCursor();
            return 0;
        case WM_ENDSESSION:  // sign-out / shutdown / installer restart manager
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DEVICECHANGE:
            if ((wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) &&
                (!g_wlmouseOnly || IsWlmouseDeviceChange(lp))) {
                g_resetBackoff = true;
                RefreshSoon(2500);
            }
            return TRUE;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) {
                g_resetBackoff = true;
                RefreshSoon(5000);
            } else if (wp == PBT_POWERSETTINGCHANGE) {
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (setting && setting->PowerSetting == kConsoleDisplayState && setting->DataLength >= sizeof(DWORD)) {
                    const bool off = *reinterpret_cast<const DWORD*>(setting->Data) == 0;  // 0 off, 1 on, 2 dimmed
                    if (g_displayOff.exchange(off) && !off) RefreshSoon(2000);
                }
            }
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) g_sessionLocked = true;
            else if (wp == WTS_SESSION_UNLOCK && g_sessionLocked.exchange(false)) RefreshSoon(2000);
            return 0;
        case WM_TIMER:
            if (wp == TIMER_DEBOUNCE) { KillTimer(h, TIMER_DEBOUNCE); SetEvent(g_refreshEvt); }
            if (wp == TIMER_WIDGET) Reconcile();
            if (wp == TIMER_TRAY_RETRY) UpdatePlaceholder();
            if (wp == TIMER_HELD_NOTES) ShowHeldNotes();
            if (wp == TIMER_ALERT_ICON) {
                KillTimer(h, TIMER_ALERT_ICON);
                if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) {
                bool light = ui::TaskbarLight();
                if (light != g_lightTaskbar) { g_lightTaskbar = light; RedrawAll(); widget::SetLightTheme(light); }
                UpdateOverview();
            }
            return 0;
        case WM_DESTROY:
            WTSUnRegisterSessionNotification(h);
            updates::Stop();
            KillTimer(h, TIMER_DEBOUNCE);
            KillTimer(h, TIMER_WIDGET);
            KillTimer(h, TIMER_ALERT_ICON);
            KillTimer(h, TIMER_TRAY_RETRY);
            KillTimer(h, TIMER_HELD_NOTES);
            settings_window::Destroy();
            if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            widget::Destroy();
            overview::Destroy();
            SetEvent(g_stopEvt);
            if (g_worker) WaitForSingleObject(g_worker, 5000);
            for (auto& [k, e] : g_entries) {
                if (e.added) TrayDelete(e.uid);
                if (e.icon) DestroyIcon(e.icon);
            }
            if (g_placeholder) TrayDelete(PLACEHOLDER_ID);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

std::wstring InstanceId() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> user(bytes);
    const bool ok = bytes && GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes);
    CloseHandle(token);
    if (!ok) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid, &sid)) return {};
    std::wstring id = sid;
    LocalFree(sid);
    wchar_t desktop[256]{};
    if (GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), nullptr)) {
        id += L"-";
        id += desktop;
    }
    if (g_demo) id += L"-demo";
    return id;
}

int StartupError(const wchar_t* message) {
    MessageBoxW(nullptr, T(message), kAppName, MB_OK | MB_ICONERROR);
    return 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    bool json = false, help = false, list = false, dump = false, quit = false, unknown = false, background = false,
         timing = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg == L"--json") json = true;
        else if (arg == L"--help") help = true;
        else if (arg == L"--list") list = true;
        else if (arg == L"--dump") dump = true;
        else if (arg == L"--quit") quit = true;
        else if (arg == L"--background") background = true;
        else if (arg == L"--demo") g_demo = true;
        else if (arg == L"--wlmouse") g_wlmouseOnly = true;
        else if (arg == L"--timing") timing = true;
        else unknown = true;
    }
    LocalFree(argv);
    if (unknown) return 2;
    if (timing) {
        std::vector<DriverTiming> timings;
        PollOptions options;
        options.timings = &timings;
        const ULONGLONG start = GetTickCount64();
        auto devices = PollAllDevices(options);
        return console::Timing(timings, devices, (unsigned)(GetTickCount64() - start));
    }
    if (json) return console::Json(PollDevices());
    if (help) {
        MessageBoxW(nullptr,
                    L"2.4BatteryMonitor options:\n\n"
                    L"--list   Print devices and battery levels\n"
                    L"--json   Print a JSON array of devices and battery levels\n"
                    L"--dump   Print every HID collection and device\n"
                    L"--timing Print how long each driver took in one poll\n"
                    L"--background Start in the notification area\n"
                    L"--wlmouse Monitor only WLMouse USB receivers and cables (also with --list or --json)\n"
                    L"--demo   Show sample devices\n"
                    L"--quit   Quit a running instance",
                    L"2.4BatteryMonitor", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (list) return console::List(PollDevices());
    if (dump) return console::Report(DumpHidCollections());
    const auto identity = InstanceId();
    if (identity.empty()) return StartupError(L"Could not identify this Windows user.");
    const auto windowTitle = std::wstring(kAppName) + L":" + identity;
    if (quit) {
        HWND w = FindWindowW(kWindowClass, windowTitle.c_str());
        if (w) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            HANDLE running = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
            if (!running) return 1;
            PostMessageW(w, WM_CLOSE, 0, 0);
            DWORD exited = WaitForSingleObject(running, 30000);
            CloseHandle(running);
            if (exited != WAIT_OBJECT_0) return 1;
        }
        return 0;
    }
    g_inst = inst;

    const auto mutexName = L"Local\\24BatteryMonitorTray-" + identity;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (!mutex) return StartupError(L"Could not start the monitor in this Windows session.");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        if (background) return 0;
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (HWND running = FindWindowW(kWindowClass, windowTitle.c_str())) {
                DWORD pid = 0;
                GetWindowThreadProcessId(running, &pid);
                if (pid) AllowSetForegroundWindow(pid);
                if (PostMessageW(running, WM_OPEN_OVERVIEW, !g_wlmouseOnly, 0)) return 0;
            }
            Sleep(50);
        }
        return StartupError(L"The running monitor did not accept the request to open.");
    }
    SetCurrentProcessExplicitAppUserModelID(app_settings::kAppUserModelId);
    app_settings::SetReadOnly(g_demo);
    if (!g_demo) app_settings::Initialize();

    g_lightTaskbar = ui::TaskbarLight();
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return StartupError(L"Could not register the monitor window.");
    // A hidden top-level window (not message-only) so it receives the TaskbarCreated broadcast.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, windowTitle.c_str(), WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return StartupError(L"Could not create the monitor window.");

    DEV_BROADCAST_DEVICEINTERFACE_W filter{ sizeof(filter) };
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    HDEVNOTIFY notify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY displayNotify = RegisterPowerSettingNotification(g_hwnd, &kConsoleDisplayState,
                                                                  DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(g_hwnd, NOTIFY_FOR_THIS_SESSION);

    g_refreshEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_refreshEvt || !g_stopEvt) return StartupError(L"Could not create the monitor's polling events.");
    g_wantWidget = app_settings::LoadBool(L"TaskbarWidget", false);
    g_hideLastKnown = app_settings::LoadBool(L"HideLastKnownDevices", false);
    g_showDisconnectedDevices = app_settings::LoadBool(L"ShowDisconnectedDevices", false);
    g_recordHistory = app_settings::LoadBool(L"RecordBatteryHistory", true);
    g_widgetBackground = app_settings::LoadBool(L"WidgetBackground", false);
    g_brandColors = app_settings::LoadBool(L"BrandColors", false);
    g_stacked = app_settings::LoadBool(L"StackedLayout", false);
    g_statusFile = app_settings::LoadBool(L"StatusFile", false);
    g_quietFullScreen = app_settings::LoadBool(L"QuietInFullScreen", true);
    g_pollIntervalMs = LoadPollSeconds() * 1000;
    widget::SetStacked(g_stacked);
    widget::SetBackground(g_widgetBackground);  // remembered by the widget across re-creation
    i18n::Init();
    g_widgetMonitor = LoadWidgetMonitor();
    widget::SetMonitorChoice(g_widgetMonitor);
    LoadHidden();
    LoadOrder();
    LoadAliases();
    LoadAlertThresholds();
    LoadChargeTargets();
    battery_history::Initialize(g_demo);
    if (!g_demo) updates::Initialize(g_hwnd, g_wlmouseOnly);
    Reconcile();
    UpdatePlaceholder();  // something to click on until the first poll finishes
    if (g_wantWidget) SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!g_worker) {
        DestroyWindow(g_hwnd);
        battery_history::Shutdown();
        return StartupError(L"Could not start the battery polling thread.");
    }
    if (!background || !g_placeholder) OpenOverview();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (HWND view = overview::Window(); view && IsWindowVisible(view) && IsDialogMessageW(view, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (notify) UnregisterDeviceNotification(notify);
    if (displayNotify) UnregisterPowerSettingNotification(displayNotify);
    battery_history::Shutdown();
    // A stalled device may outlive the bounded window shutdown wait. In that case let Windows
    // terminate the process with the worker still running, before C++ globals can be destroyed.
    if (WaitForSingleObject(g_worker, 0) != WAIT_OBJECT_0) ExitProcess(0);
    CloseHandle(g_worker);
    CloseHandle(g_refreshEvt);
    CloseHandle(g_stopEvt);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
