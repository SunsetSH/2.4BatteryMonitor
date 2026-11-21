#pragma once
// Text of a device report for a GitHub issue (device_report.cpp). Portable and tested: nothing here
// sends anything; the app only opens the prefilled issue page, the user reads it and submits it.
#include <cstdio>
#include <string>
#include <vector>

#include "devices.h"

namespace report_text {

// Percent-encodes UTF-8 for a URL query value (RFC 3986 unreserved characters stay as they are).
inline std::string UrlEncode(const std::string& utf8) {
    std::string out;
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += (char)c;
        } else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            out += b;
        }
    }
    return out;
}

inline std::wstring Hex4(unsigned v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v & 0xFFFF);
    return b;
}

// Lines of a `--dump` report that mention "VID:PID" (the header line is kept), or every line when
// `vid` is 0. Keeps the report about the device the user asked about.
inline std::wstring DumpFor(const std::wstring& dump, unsigned short vid, unsigned short pid) {
    if (!vid) return dump;
    const std::wstring id = Hex4(vid) + L":" + Hex4(pid);
    std::wstring out;
    size_t pos = 0;
    bool first = true;
    while (pos < dump.size()) {
        size_t end = dump.find(L'\n', pos);
        if (end == std::wstring::npos) end = dump.size();
        std::wstring line = dump.substr(pos, end - pos);
        if (first || line.find(id) != std::wstring::npos) out += line + L"\n";
        first = false;
        pos = end + 1;
    }
    return out;
}

// Markdown issue body. `version` and `windows` describe this copy; `device` may be null for
// "my device is not listed".
inline std::wstring Body(const DeviceStatus* device, const std::wstring& version, const std::wstring& windows,
                         const std::wstring& dump) {
    std::wstring b = L"**App version:** " + version + L"\n**Windows:** " + windows + L"\n\n";
    if (device) {
        b += L"**Device:** " + device->name + L"\n**Key:** `" + device->key + L"`\n**Connection:** " + device->via +
             L"\n**VID:PID:** " + (device->vid ? Hex4(device->vid) + L":" + Hex4(device->pid) : L"unknown") +
             L"\n**Reading:** " + (device->percent >= 0 ? std::to_wstring(device->percent) + L"%" : L"none") +
             (device->charging ? L", charging" : L"") + (device->asleep ? L", asleep" : L"") + L"\n\n";
        b += L"**Does the level match the vendor app or the device?** (yes / no, what it shows)\n\n";
    } else {
        b += L"**Device model and how it connects** (receiver, cable, Bluetooth):\n\n";
    }
    b += L"<details><summary>--dump</summary>\n\n```\n" + dump + L"```\n</details>\n";
    return b;
}

// A "new issue" URL. `body(dump)` makes the UTF-8 body for a dump text; when the URL would be longer
// than `maxUrl` (browsers and GitHub reject very long ones), dump lines are dropped from the end and a
// note says so, keeping the rest of the body intact. `cut` tells whether that happened.
template <class MakeBody>
std::string IssueUrl(const std::string& repo, const std::string& titleUtf8, const std::wstring& dump, MakeBody body,
                     size_t maxUrl, bool& cut) {
    const std::string head = "https://github.com/" + repo + "/issues/new?title=" + UrlEncode(titleUtf8) + "&body=";
    const std::wstring note = L"... cut, run `2.4BatteryMonitor.exe --dump` for the full list\n";
    std::wstring kept = dump;
    cut = false;
    for (;;) {
        std::string url = head + UrlEncode(body(cut ? kept + note : kept));
        if (url.size() <= maxUrl || kept.empty()) return url;
        // Drop the last line (the text before its final line break stays).
        size_t nl = kept.size() > 1 ? kept.rfind(L'\n', kept.size() - 2) : std::wstring::npos;
        kept.resize(nl == std::wstring::npos ? 0 : nl + 1);
        cut = true;
    }
}

}  // namespace report_text
