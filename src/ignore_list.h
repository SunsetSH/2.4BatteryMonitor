#pragma once
// IgnoreDevices (REG_SZ): "VID:PID" pairs in hex, separated by spaces, commas or semicolons.
// One parser for the poll (devices.cpp) and the settings window, so both see the same list.
#include <string>
#include <utility>
#include <vector>

namespace ignore_list {
using Id = std::pair<unsigned short, unsigned short>;  // vid, pid

inline bool Separator(wchar_t c) {
    return c == L',' || c == L';' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

// A 16-bit hex number, with or without "0x" and leading zeros (the poll used to read the list with
// swscanf "%x", which accepts both); false for anything else.
inline bool ParseHex16(std::wstring s, unsigned short& out) {
    if (s.size() > 2 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) s = s.substr(2);
    if (s.empty() || s.size() > 8) return false;
    unsigned v = 0;
    for (wchar_t c : s) {
        int d;
        if (c >= L'0' && c <= L'9')
            d = c - L'0';
        else if (c >= L'a' && c <= L'f')
            d = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F')
            d = c - L'A' + 10;
        else
            return false;
        v = v * 16 + (unsigned)d;
    }
    if (v > 0xFFFF) return false;
    out = (unsigned short)v;
    return true;
}

// Entries that are not "VID:PID" are skipped, and so is vendor id 0: it means "no id" in DeviceStatus
// and would match every device without one. A repeated entry counts once.
inline std::vector<Id> Parse(const std::wstring& text) {
    std::vector<Id> out;
    for (size_t i = 0; i < text.size();) {
        while (i < text.size() && Separator(text[i])) i++;
        size_t end = i;
        while (end < text.size() && !Separator(text[end])) end++;
        const std::wstring entry = text.substr(i, end - i);
        i = end;
        const size_t colon = entry.find(L':');
        Id id;
        if (colon == std::wstring::npos || !ParseHex16(entry.substr(0, colon), id.first) ||
            !ParseHex16(entry.substr(colon + 1), id.second) || !id.first)
            continue;
        bool seen = false;
        for (const auto& o : out) seen |= o == id;
        if (!seen) out.push_back(id);
    }
    return out;
}

// "046D:C52B": the form the settings window shows and saves.
inline std::wstring Format(Id id) {
    const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring s;
    for (unsigned v : { (unsigned)id.first, (unsigned)id.second }) {
        if (!s.empty()) s += L':';
        for (int shift = 12; shift >= 0; shift -= 4) s += hex[(v >> shift) & 0xF];
    }
    return s;
}

inline std::wstring Join(const std::vector<Id>& ids) {
    std::wstring text;
    for (const auto& id : ids) text += (text.empty() ? L"" : L",") + Format(id);
    return text;
}

inline bool Contains(const std::vector<Id>& ids, unsigned short vid, unsigned short pid) {
    for (const auto& id : ids)
        if (id.first == vid && id.second == pid) return true;
    return false;
}

// Removes ignored devices from poll results (DeviceStatus or anything with vid / pid). Sources without
// HID collections (XInput, Bluetooth LE adverts) are not filtered before the poll, so this is what
// keeps them off the screen.
template <class Status> void DropIgnored(std::vector<Status>& results, const std::vector<Id>& ids) {
    if (ids.empty()) return;
    std::vector<Status> kept;
    for (auto& s : results)
        if (!s.vid || !Contains(ids, s.vid, s.pid)) kept.push_back(std::move(s));
    results.swap(kept);
}
}  // namespace ignore_list
