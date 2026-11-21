// The Windows HID backend. Linux uses hid_linux.cpp instead; this file is compiled only when
// _WIN32 is defined. Portable helpers (Hex4, GuessKind, ...) live in hid_util.cpp.
#ifdef _WIN32

#include "hid.h"

#include <setupapi.h>
#include <hidclass.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <map>
#include <mutex>

namespace {

// Capabilities and product strings by device interface path. A path stays bound to one device
// interface while it exists, so a cached entry never goes stale; entries for paths that disappeared
// are dropped on the next full enumeration.
std::mutex g_cacheLock;
std::map<std::wstring, HidColl> g_collCache;
std::map<std::wstring, std::wstring> g_productCache;

bool OpenCaps(const std::wstring& path, HidColl& c) {
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    c.path = path;
    HIDD_ATTRIBUTES a{ sizeof(a) };
    if (HidD_GetAttributes(h, &a)) {
        c.vid = a.VendorID;
        c.pid = a.ProductID;
    }
    PHIDP_PREPARSED_DATA pp;
    if (HidD_GetPreparsedData(h, &pp)) {
        HidP_GetCaps(pp, &c.caps);
        HidD_FreePreparsedData(pp);
    }
    CloseHandle(h);
    std::wstring lower = c.path;
    for (auto& ch : lower) ch = towlower(ch);
    size_t mi = lower.find(L"&mi_");
    if (mi != std::wstring::npos) c.iface = (int)wcstol(lower.c_str() + mi + 4, nullptr, 16);
    return true;
}

std::vector<HidColl> EnumHidCollections(USHORT usbVid) {
    std::vector<HidColl> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO di = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return out;
    const std::wstring prefix = usbVid ? L"\\\\?\\hid#vid_" + Hex4(usbVid) + L"&pid_" : L"";
    std::vector<BYTE> buf;
    std::map<std::wstring, HidColl> seen;
    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        buf.resize(need);
        auto det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, det, need, nullptr, nullptr)) continue;
        if (usbVid && _wcsnicmp(det->DevicePath, prefix.c_str(), prefix.size()) != 0) continue;
        const std::wstring path = det->DevicePath;
        HidColl c;
        bool cached = false;
        {
            std::lock_guard<std::mutex> lock(g_cacheLock);
            auto it = g_collCache.find(path);
            if (it != g_collCache.end()) {
                c = it->second;
                cached = true;
            }
        }
        if (!cached && !OpenCaps(path, c)) continue;
        seen[path] = c;
        if (usbVid && c.vid != usbVid) continue;
        out.push_back(std::move(c));
    }
    SetupDiDestroyDeviceInfoList(di);
    std::lock_guard<std::mutex> lock(g_cacheLock);
    if (usbVid) {
        for (auto& [path, c] : seen) g_collCache[path] = c;
    } else {
        g_collCache = std::move(seen);
        for (auto it = g_productCache.begin(); it != g_productCache.end();)
            it = g_collCache.count(it->first) ? std::next(it) : g_productCache.erase(it);
    }
    return out;
}

}  // namespace

std::vector<HidColl> EnumHid() { return EnumHidCollections(0); }
std::vector<HidColl> EnumUsbHidForVendor(USHORT vid) { return EnumHidCollections(vid); }

// ---- HidDev -----------------------------------------------------------------------------------

bool HidDev::Open(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                     OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    overlapped_ = true;
    return ok();
}

bool HidDev::OpenReadOnly(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                     FILE_FLAG_OVERLAPPED, nullptr);
    overlapped_ = true;
    return ok();
}

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    overlapped_ = false;
    return ok();
}

void HidDev::Close() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
    if (ev_) CloseHandle(ev_);
    ev_ = nullptr;
}

// Waits for an overlapped call started with `ov`; cancels it after `timeoutMs` (0 = do not wait).
// Returns whether it completed, with the byte count in `got`.
bool HidDev::Wait(OVERLAPPED& ov, DWORD& got, int timeoutMs) {
    if (timeoutMs > 0 && WaitForSingleObject(ov.hEvent, (DWORD)timeoutMs) == WAIT_OBJECT_0)
        return GetOverlappedResult(h_, &ov, &got, FALSE) != FALSE;
    CancelIoEx(h_, &ov);
    return GetOverlappedResult(h_, &ov, &got, TRUE) != FALSE;  // it may have completed meanwhile
}

namespace {

// Prepares `ov` with the handle's reusable event; false when no event could be created.
bool Arm(HANDLE& ev, OVERLAPPED& ov) {
    if (!ev) ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev) return false;
    ResetEvent(ev);
    ov = OVERLAPPED{};
    ov.hEvent = ev;
    return true;
}

}  // namespace

bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.outLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.outLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    OVERLAPPED ov;
    if (!Arm(ev_, ov)) return false;
    DWORD n = 0;
    if (!WriteFile(h_, buf.data(), (DWORD)buf.size(), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
    return Wait(ov, n, timeoutMs);
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.inLen()) return -1;
    std::vector<BYTE> tmp(c_.inLen(), 0);
    OVERLAPPED ov;
    if (!Arm(ev_, ov)) return -1;
    DWORD got = 0;
    if (!ReadFile(h_, tmp.data(), (DWORD)tmp.size(), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return -1;
    if (!Wait(ov, got, timeoutMs) || got == 0) return -1;
    size_t off = tmp[0] == 0 ? 1 : 0;  // hidapi semantics: drop the "no report ID" byte
    buf.assign(tmp.begin() + off, tmp.begin() + got);
    return (int)buf.size();
}

int HidDev::SetOutputAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.inLen() || !c_.outLen() || data.empty()) return -1;
    std::vector<BYTE> out(c_.outLen(), 0);
    memcpy(out.data(), data.data(), std::min(out.size(), data.size()));
    std::vector<BYTE> tmp(c_.inLen(), 0);
    OVERLAPPED ov;
    if (!Arm(ev_, ov)) return -1;
    DWORD got = 0;
    if (!ReadFile(h_, tmp.data(), (DWORD)tmp.size(), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return -1;
    bool sent = HidD_SetOutputReport(h_, out.data(), (ULONG)out.size()) != FALSE;
    bool done = Wait(ov, got, sent ? timeoutMs : 0) && sent;
    if (!done || got == 0) return -1;
    size_t off = tmp[0] == 0 ? 1 : 0;
    buf.assign(tmp.begin() + off, tmp.begin() + got);
    return (int)buf.size();
}

void HidDev::Flush() {
    if (ok()) HidD_FlushQueue(h_);
}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    if (!ok() || !c_.featLen()) return false;
    std::vector<BYTE> buf(c_.featLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    return HidD_SetFeature(h_, buf.data(), (ULONG)buf.size()) != FALSE;
}

bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.featLen()) return false;
    out.assign(c_.featLen(), 0);
    out[0] = reportId;
    return HidD_GetFeature(h_, out.data(), (ULONG)out.size()) != FALSE;
}

bool HidDev::GetInputReport(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.inLen()) return false;
    out.assign(c_.inLen(), 0);
    out[0] = reportId;
    return HidD_GetInputReport(h_, out.data(), (ULONG)out.size()) != FALSE;
}

bool HidDev::GetFeatureByte(BYTE reportId, BYTE& value, int timeoutMs) {
    if (!ok() || !overlapped_ || !reportId || c_.featLen() < 2) return false;
    BYTE report[] = { reportId, 0xff };
    OVERLAPPED ov;
    if (!Arm(ev_, ov)) return false;
    DWORD received = 0;
    BOOL done =
        DeviceIoControl(h_, IOCTL_HID_GET_FEATURE, report, sizeof(report), report, sizeof(report), &received, &ov);
    if (!done && GetLastError() == ERROR_IO_PENDING) done = Wait(ov, received, timeoutMs);
    if (!done || received != sizeof(report) || report[0] != reportId) return false;
    value = report[1];
    return true;
}

// ---- helpers ----------------------------------------------------------------------------------

std::wstring HidProduct(const HidColl& c) {
    {
        std::lock_guard<std::mutex> lock(g_cacheLock);
        auto it = g_productCache.find(c.path);
        if (it != g_productCache.end()) return it->second;
    }
    HANDLE h = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wchar_t name[128] = {};
    bool read = HidD_GetProductString(h, name, sizeof(name) - sizeof(wchar_t)) != FALSE;
    if (!read) name[0] = 0;
    CloseHandle(h);
    std::wstring s = name;
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    if (read) {
        std::lock_guard<std::mutex> lock(g_cacheLock);
        g_productCache[c.path] = s;
    }
    return s;
}

#endif  // _WIN32
