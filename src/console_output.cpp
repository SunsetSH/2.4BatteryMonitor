// Command-line output. See console_output.h.
#include "console_output.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "brands.h"
#include "json_output.h"
#include "status_text.h"

namespace console {

namespace {

// The parent's console when started from one; otherwise none.
HANDLE Output() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((!out || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS))
        out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    return out;
}

bool Usable(HANDLE out) { return out && out != INVALID_HANDLE_VALUE; }

void Write(HANDLE out, const std::string& utf8) {
    if (!Usable(out)) return;
    DWORD written;
    WriteFile(out, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
}

std::string Utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u.data(), n, nullptr, nullptr);
    return u;
}

std::wstring English(const wchar_t* phrase) { return phrase; }

}  // namespace

int List(const std::vector<DeviceStatus>& devices) {
    HANDLE out = Output();
    if (devices.empty()) Write(out, "No devices with battery info found.\n");
    for (const auto& s : devices) {
        Accent accent = BrandOf(s);
        std::wstring line = s.name + L"  [" + s.via + L"]  " +
                            (accent != Accent::None ? L"{" + std::wstring(StyleOf(accent).name) + L"}  " : L"");
        line += status_text::Describe(s, -1, English);
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        Write(out, Utf8(line + L"   (" + s.key + L")\n"));
    }
    return 0;
}

int Json(const std::vector<DeviceStatus>& devices) {
    Write(Output(), DevicesToJson(devices));
    return 0;
}

int Report(const std::wstring& text) {
    HANDLE out = Output();
    if (Usable(out)) {
        Write(out, Utf8(text));
        return 0;
    }
    // With no console, open a new local report. Never overwrite an existing report.
    wchar_t temp[MAX_PATH] = {};
    DWORD size = GetTempPathW(ARRAYSIZE(temp), temp);
    if (!size || size >= ARRAYSIZE(temp)) return 1;
    wchar_t path[MAX_PATH] = {};
    if (swprintf(path, ARRAYSIZE(path), L"%ls2.4BatteryMonitor-dump-%lu-%llu.txt", temp, GetCurrentProcessId(),
                 GetTickCount64()) < 0)
        return 1;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 1;
    std::wstring crlf;
    crlf.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L'\n') crlf += L'\r';
        crlf += ch;
    }
    std::string u = "\xEF\xBB\xBF" + Utf8(crlf);
    DWORD written = 0;
    bool saved = WriteFile(f, u.data(), (DWORD)u.size(), &written, nullptr) && written == u.size();
    CloseHandle(f);
    if (!saved) {
        DeleteFileW(path);
        return 1;
    }
    return (INT_PTR)ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL) > 32 ? 0 : 1;
}

int Timing(const std::vector<DriverTiming>& timings, const std::vector<DeviceStatus>& devices, unsigned wallMs) {
    HANDLE out = Output();
    std::vector<DriverTiming> sorted = timings;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const DriverTiming& a, const DriverTiming& b) { return a.ms > b.ms; });
    unsigned sum = 0;
    char line[160];
    Write(out, "Driver               Lane          ms  Found\n");
    for (const auto& t : sorted) {
        sum += t.ms;
        snprintf(line, sizeof line, "%-20s %-10s %6u  %d%s\n", t.name.c_str(), t.lane.empty() ? "main" : t.lane.c_str(),
                 t.ms, t.found, t.skipped ? "  (skipped)" : "");
        Write(out, line);
    }
    snprintf(line, sizeof line, "\n%zu drivers, %u ms in drivers, %u ms wall clock, %zu devices\n", sorted.size(), sum,
             wallMs, devices.size());
    Write(out, line);
    return 0;
}

}  // namespace console
