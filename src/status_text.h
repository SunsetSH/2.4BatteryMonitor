#pragma once
// How a device's reading is shown: one set of rules for the tray icon, the taskbar widget, the device
// overview and `--list`. Portable (no Win32), so it is tested without windows.h and the Linux build
// can share it.
#include <string>
#include <utility>
#include <vector>

#include "devices.h"

namespace status_text {

// What the reading means for display. `lastPct` is the last known level of a device that is quiet now.
struct Shown {
    int percent = -1;      // level to draw: the fresh one, else the last known one, -1 if none
    bool offline = false;  // the driver says the wireless link is down: never show a stale level
    bool charging = false; // on power and still charging (never together with `full`)
    bool full = false;     // on power and fully charged
    bool dim = false;      // asleep, cached or unknown right now
};

inline Shown ShownState(const DeviceStatus& s, int lastPct) {
    Shown v;
    v.offline = !s.connected;
    v.percent = v.offline ? -1 : (s.percent >= 0 ? s.percent : lastPct);
    v.dim = !s.HasFreshBattery();
    const bool live = s.connected && !s.asleep && !s.cached;
    v.full = live && (s.full || (s.charging && v.percent >= 100));
    v.charging = live && s.charging && !v.full;
    return v;
}

enum class Level { Normal, Warn, Low };

// Red at or below the device's low-battery threshold, orange in the 15 points above it. A charging
// or offline device is never shown as low.
inline Level LevelOf(const Shown& v, int lowThreshold) {
    if (v.offline || v.charging || v.full || v.percent < 0) return Level::Normal;
    if (v.percent <= lowThreshold) return Level::Low;
    if (v.percent <= lowThreshold + 15) return Level::Warn;
    return Level::Normal;
}

// Replaces {name} placeholders in a translated phrase.
inline std::wstring Fill(std::wstring text, const std::vector<std::pair<std::wstring, std::wstring>>& args) {
    for (const auto& [name, value] : args) {
        const std::wstring tag = L"{" + name + L"}";
        for (size_t at = text.find(tag); at != std::wstring::npos; at = text.find(tag, at + value.size()))
            text.replace(at, tag.size(), value);
    }
    return text;
}

// Left / right / case levels as "L 80%, R 75%, case 40%" (empty when the device has none).
template <class Translate> std::wstring BudsText(const DeviceStatus& s, Translate tr) {
    std::wstring out;
    auto add = [&](const wchar_t* phrase, int pct) {
        if (pct < 0) return;
        if (!out.empty()) out += L", ";
        out += Fill(tr(phrase), { { L"percent", std::to_wstring(pct) } });
    };
    add(L"L {percent}%", s.left);
    add(L"R {percent}%", s.right);
    add(L"case {percent}%", s.casePct);
    return out;
}

// The level and state in words, e.g. "85% (L 85%, R 90%), charging". `tr` translates an English
// phrase (identity for `--list`).
template <class Translate> std::wstring Describe(const DeviceStatus& s, int lastPct, Translate tr) {
    if (!s.connected) return tr(L"off / not connected");
    const Shown v = ShownState(s, lastPct);
    std::wstring t;
    if (s.cached && s.percent >= 0)
        t = Fill(tr(L"{percent}% (last known)"), { { L"percent", std::to_wstring(s.percent) } });
    else if (!s.levelText.empty() && s.percent >= 0)
        t = tr(s.levelText.c_str());
    else if (s.percent >= 0)
        t = std::to_wstring(s.percent) + L"%";
    else if (lastPct >= 0)
        t = Fill(tr(L"{percent}% (last known)"), { { L"percent", std::to_wstring(lastPct) } });
    else
        t = tr(L"unknown");
    std::wstring parts = BudsText(s, tr);
    if (parts.empty()) parts = s.detail;
    if (!parts.empty() && s.percent >= 0) t += L" (" + parts + L")";
    if (v.full)
        t += std::wstring(L", ") + tr(L"fully charged");
    else if (v.charging)
        t += std::wstring(L", ") + tr(L"charging");
    if (s.asleep) t += std::wstring(L", ") + tr(L"asleep");
    return t;
}

}  // namespace status_text
