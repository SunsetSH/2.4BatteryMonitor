// Device report as a prefilled GitHub issue. See device_report.h.
#include "device_report.h"

#include <shellapi.h>

#include "../build/version.h"
#include "i18n.h"
#include "platform_compat.h"
#include "report_text.h"

namespace device_report {

namespace {

// "10.0.26200": RtlGetVersion reports the real build, GetVersionEx the manifest's view of it.
std::wstring WindowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW v{ sizeof(v) };
    if (!get || get(&v) != 0) return L"unknown";
    return std::to_wstring(v.dwMajorVersion) + L"." + std::to_wstring(v.dwMinorVersion) + L"." +
           std::to_wstring(v.dwBuildNumber);
}

}  // namespace

void Open(HWND owner, const DeviceStatus* device) {
    const std::wstring question =
        std::wstring(T(L"Open a new GitHub issue in your browser with this device's details filled in?")) + L"\n\n" +
        T(L"Nothing is sent until you submit the issue on GitHub. You can edit or remove any detail first.");
    if (MessageBoxW(owner, question.c_str(), L"2.4BatteryMonitor", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return;

    const std::wstring version = Utf8ToWide(WB_VERSION_STRING), windows = WindowsVersion();
    const std::wstring dump =
        report_text::DumpFor(DumpHidCollections(), device ? device->vid : 0, device ? device->pid : 0);
    const std::wstring title = device ? L"Device report: " + device->name : L"Device not listed: ";
    bool cut = false;
    const std::string url = report_text::IssueUrl(
        WideToUtf8(WB_UPDATE_REPO_W), WideToUtf8(title), dump,
        [&](const std::wstring& d) { return WideToUtf8(report_text::Body(device, version, windows, d)); }, 7000, cut);
    ShellExecuteW(owner, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace device_report
