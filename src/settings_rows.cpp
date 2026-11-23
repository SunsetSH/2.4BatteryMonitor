// Rows of the settings window. See settings_rows.h.
#include "settings_rows.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>

#include "app_settings.h"
#include "device_report.h"
#include "i18n.h"
#include "ignore_list.h"
#include "ui_theme.h"
#include "update.h"
#include "widget.h"

namespace {

constexpr unsigned long kPollSeconds[] = { 30, 60, 120, 300, 600, 900, 1800, 3600 };
constexpr DWORD kOffsets[] = { 0, 4, 8, 16, 24, 32, 48, 64 };

// A popup menu of choices at `at`; returns the chosen index, or -1.
int Choose(HWND owner, POINT at, const std::vector<std::wstring>& items, int checked) {
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].empty())
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        else
            AppendMenuW(m, MF_STRING | ((int)i == checked ? MF_CHECKED : 0), i + 1, items[i].c_str());
    }
    ui::ApplyMenuTheme(ui::AppsLight());
    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | (i18n::Rtl() ? TPM_LAYOUTRTL : 0),
                              at.x, at.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(m);
    return cmd ? (int)cmd - 1 : -1;
}

std::wstring OnOff(bool on) { return on ? T(L"On") : T(L"Off"); }

std::wstring PollLabel(unsigned long s) {
    return s < 60 ? TF(L"{seconds} s", { { L"seconds", std::to_wstring(s) } })
                  : TF(L"{minutes} min", { { L"minutes", std::to_wstring(s / 60) } });
}

std::wstring DataFolder() {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) return L"";
    return std::wstring(base) + L"\\2.4BatteryMonitor";
}

// IgnoreDevices, read with the poll's parser (ignore_list.h) and written back in its "046D:C52B" form.
std::vector<std::wstring> IgnoredIds() {
    std::vector<std::wstring> out;
    for (const auto& id : ignore_list::Parse(app_settings::LoadText(L"IgnoreDevices")))
        out.push_back(ignore_list::Format(id));
    return out;
}

void SaveIgnored(const std::vector<std::wstring>& ids) {
    std::wstring text;
    for (const auto& id : ids) text += (text.empty() ? L"" : L",") + id;
    app_settings::SaveText(L"IgnoreDevices", text);
}

std::wstring IdOf(const DeviceStatus& d) { return ignore_list::Format({ d.vid, d.pid }); }

settings_window::Row Switch(const SettingsApi& api, const wchar_t* name, const wchar_t* title, const wchar_t* detail,
                            bool enabled = true) {
    settings_window::Row r;
    r.title = T(title);
    if (detail) r.detail = T(detail);
    r.value = OnOff(api.getSwitch(name));
    r.enabled = enabled;
    std::wstring key = name;
    r.activate = [&api, key](HWND, POINT) { api.setSwitch(key, !api.getSwitch(key)); };
    return r;
}

void AddDisplay(const SettingsApi& api, std::vector<settings_window::Row>& rows) {
    const bool widgetMode = api.getSwitch(L"TaskbarWidget");
    settings_window::Row mode;
    mode.section = T(L"Display");
    mode.title = T(L"Show battery levels");
    mode.value = widgetMode ? T(L"Taskbar widget") : T(L"Tray icons");
    mode.activate = [&api](HWND w, POINT at) {
        const bool widget = api.getSwitch(L"TaskbarWidget");
        int choice =
            Choose(w, at, { T(L"Show on the taskbar (icon + percentage)"), T(L"Show as tray icons") }, widget ? 0 : 1);
        if (choice >= 0) api.setSwitch(L"TaskbarWidget", choice == 0);
    };
    rows.push_back(mode);
    rows.push_back(Switch(api, L"WidgetBackground", L"Background behind taskbar text (for transparent taskbars)",
                          nullptr, widgetMode));
    rows.push_back(Switch(api, L"StackedLayout", L"Icons above percentages (narrower)", nullptr, widgetMode));

    auto monitors = widget::OtherMonitors();
    settings_window::Row where;
    where.title = T(L"Show the widget on");
    const std::wstring chosen = api.widgetMonitor();
    where.value = chosen == L"main" ? T(L"Main monitor only") : T(L"All monitors");
    for (const auto& m : monitors)
        if (m.id == chosen) where.value = TF(L"{monitor} only", { { L"monitor", m.label } });
    where.enabled = widgetMode && !monitors.empty();
    if (monitors.empty()) where.detail = T(L"Only one monitor is connected.");
    where.activate = [&api, monitors](HWND w, POINT at) {
        std::vector<std::wstring> items{ T(L"All monitors"), T(L"Main monitor only") };
        int checked = api.widgetMonitor() == L"main" ? 1 : 0;
        for (size_t i = 0; i < monitors.size(); i++) {
            items.push_back(TF(L"{monitor} only", { { L"monitor", monitors[i].label } }));
            if (monitors[i].id == api.widgetMonitor()) checked = (int)i + 2;
        }
        int choice = Choose(w, at, items, checked);
        if (choice == 0)
            api.setWidgetMonitor(L"all");
        else if (choice == 1)
            api.setWidgetMonitor(L"main");
        else if (choice >= 2)
            api.setWidgetMonitor(monitors[choice - 2].id);
    };
    rows.push_back(where);

    settings_window::Row offset;
    offset.title = T(L"Widget offset");
    offset.detail = T(L"Moves the widget away from the clock, for taskbar mods that add their own buttons.");
    const DWORD current = app_settings::LoadDword(L"TaskbarOffset", 0);
    offset.value = TF(L"{pixels} px", { { L"pixels", std::to_wstring(current) } });
    offset.enabled = widgetMode;
    offset.activate = [](HWND w, POINT at) {
        std::vector<std::wstring> items;
        int checked = -1;
        const DWORD now = app_settings::LoadDword(L"TaskbarOffset", 0);
        for (size_t i = 0; i < std::size(kOffsets); i++) {
            items.push_back(TF(L"{pixels} px", { { L"pixels", std::to_wstring(kOffsets[i]) } }));
            if (kOffsets[i] == now) checked = (int)i;
        }
        int choice = Choose(w, at, items, checked);
        if (choice < 0) return;
        app_settings::SaveDword(L"TaskbarOffset", kOffsets[choice]);
        widget::ReloadSettings();
    };
    rows.push_back(offset);
    rows.push_back(Switch(api, L"BrandColors", L"Brand colors (Razer green, Logitech blue, ...)", nullptr));
}

void AddDevices(const SettingsApi& api, std::vector<settings_window::Row>& rows) {
    settings_window::Row poll;
    poll.section = T(L"Devices");
    poll.title = T(L"Check battery every");
    poll.value = PollLabel(api.pollSeconds());
    poll.activate = [&api](HWND w, POINT at) {
        std::vector<std::wstring> items;
        int checked = -1;
        for (size_t i = 0; i < std::size(kPollSeconds); i++) {
            items.push_back(PollLabel(kPollSeconds[i]));
            if (kPollSeconds[i] == api.pollSeconds()) checked = (int)i;
        }
        int choice = Choose(w, at, items, checked);
        if (choice >= 0) api.setPollSeconds(kPollSeconds[choice]);
    };
    rows.push_back(poll);
    rows.push_back(Switch(api, L"HideLastKnownDevices", L"Hide last known / asleep devices", nullptr));
    rows.push_back(Switch(api, L"ShowDisconnectedDevices", L"Show disconnected devices", nullptr));

    settings_window::Row ignored;
    ignored.title = T(L"Ignored devices");
    ignored.detail = T(L"Never queried or shown. Useful when another app needs a device to itself.");
    const auto ids = IgnoredIds();
    ignored.value = ids.empty() ? T(L"None") : TF(L"{count} devices", { { L"count", std::to_wstring(ids.size()) } });
    ignored.enabled = !api.demo;
    ignored.activate = [&api](HWND w, POINT at) {
        auto ids = IgnoredIds();
        std::vector<std::wstring> items, itemIds;
        int checked = -1;
        for (const auto& d : api.devices()) {
            if (!d.vid) continue;
            const std::wstring id = IdOf(d);
            if (std::find(itemIds.begin(), itemIds.end(), id) != itemIds.end()) continue;
            items.push_back(d.name + L"  (" + id + L")");
            itemIds.push_back(id);
        }
        for (const auto& id : ids)  // ignored devices are not polled, so not in the list above
            if (std::find(itemIds.begin(), itemIds.end(), id) == itemIds.end()) {
                items.push_back(TF(L"Ignored: {id}", { { L"id", id } }));
                itemIds.push_back(id);
            }
        if (!ids.empty()) {
            items.push_back(L"");
            items.push_back(T(L"Clear the list"));
        }
        if (items.empty()) return;
        // Checked items are the ignored ones; a popup menu can only show one check per call, so the
        // labels carry the state instead.
        for (size_t i = 0; i < itemIds.size(); i++)
            if (std::find(ids.begin(), ids.end(), itemIds[i]) != ids.end()) items[i] = L"✓  " + items[i];
        int choice = Choose(w, at, items, checked);
        if (choice < 0) return;
        if ((size_t)choice >= itemIds.size()) {
            ids.clear();
        } else {
            auto it = std::find(ids.begin(), ids.end(), itemIds[choice]);
            if (it != ids.end())
                ids.erase(it);
            else
                ids.push_back(itemIds[choice]);
        }
        SaveIgnored(ids);
        api.refreshDevices();
    };
    rows.push_back(ignored);

    settings_window::Row report;
    report.title = T(L"Report a missing device...");
    report.detail = T(L"Opens a prefilled GitHub issue in your browser. Nothing is sent until you submit it.");
    report.enabled = !api.demo;
    report.activate = [](HWND w, POINT) { device_report::Open(w, nullptr); };
    rows.push_back(report);
}

void AddGeneral(const SettingsApi& api, std::vector<settings_window::Row>& rows) {
    settings_window::Row quiet = Switch(api, L"QuietInFullScreen", L"Quiet in full-screen apps",
                                        L"Holds notifications while a game or presentation runs full screen.");
    quiet.section = T(L"Notifications");
    rows.push_back(quiet);

    settings_window::Row history = Switch(api, L"RecordBatteryHistory", L"Record battery history", nullptr);
    history.section = T(L"History and other apps");
    rows.push_back(history);
    rows.push_back(Switch(api, L"StatusFile", L"Write status.json for other apps",
                          L"Rainmeter, Stream Deck or scripts can read it; the same fields as --json."));
    settings_window::Row folder;
    folder.title = T(L"Open data folder");
    folder.detail = DataFolder();
    folder.enabled = !api.demo;
    folder.activate = [](HWND w, POINT) {
        const std::wstring dir = DataFolder();
        CreateDirectoryW(dir.c_str(), nullptr);
        ShellExecuteW(w, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    rows.push_back(folder);

    settings_window::Row language;
    language.section = T(L"General");
    language.title = T(L"Language");
    i18n::ReloadLanguages();
    auto languages = i18n::Available();
    language.value = T(L"Automatic (Windows language)");
    for (const auto& l : languages)
        if (!i18n::Choice().empty() && _wcsicmp(l.code.c_str(), i18n::Choice().c_str()) == 0) language.value = l.name;
    language.activate = [&api, languages](HWND w, POINT at) {
        std::vector<std::wstring> items{ T(L"Automatic (Windows language)") };
        int checked = 0;
        for (size_t i = 0; i < languages.size(); i++) {
            items.push_back(languages[i].name);
            if (!i18n::Choice().empty() && _wcsicmp(languages[i].code.c_str(), i18n::Choice().c_str()) == 0)
                checked = (int)i + 1;
        }
        if (!api.demo) {
            items.push_back(L"");
            items.push_back(T(L"Add your own language..."));
        }
        int choice = Choose(w, at, items, checked);
        if (choice == 0)
            api.setLanguage(L"");
        else if (choice > 0 && (size_t)choice <= languages.size())
            api.setLanguage(languages[choice - 1].code);
        else if (choice > 0 && (size_t)choice == languages.size() + 2)
            i18n::OpenUserFolder();
    };
    rows.push_back(language);

    if (!api.demo) rows.push_back(Switch(api, L"Autostart", L"Start with Windows", nullptr));
    settings_window::Row updates;
    updates.title = T(L"Updates");
    updates.value = T(L"Check for updates...");
    updates.enabled = !api.demo;
    updates.activate = [](HWND w, POINT at) {
        HMENU m = CreatePopupMenu();
        updates::AddMenu(m);
        HMENU sub = GetSubMenu(m, 0);
        if (sub) {
            ui::ApplyMenuTheme(ui::AppsLight());
            SetForegroundWindow(w);
            UINT cmd = TrackPopupMenu(sub, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN, at.x, at.y, 0, w, nullptr);
            PostMessageW(w, WM_NULL, 0, 0);
            if (cmd) updates::MenuCommand(cmd);
        }
        DestroyMenu(m);
    };
    rows.push_back(updates);
}

}  // namespace

std::vector<settings_window::Row> SettingsRows(const SettingsApi& api) {
    std::vector<settings_window::Row> rows;
    AddDisplay(api, rows);
    AddDevices(api, rows);
    AddGeneral(api, rows);
    return rows;
}
