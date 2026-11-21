// status.json for other apps. See status_file.h.
#include "status_file.h"

#include <windows.h>
#include <shlobj.h>

#include <string>

#include "json_output.h"

namespace status_file {

bool Write(const std::vector<DeviceStatus>& devices) {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) return false;
    const std::wstring dir = std::wstring(base) + L"\\2.4BatteryMonitor";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\status.json", temp = path + L".tmp";
    const std::string json = DevicesToJson(devices);
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, json.data(), (DWORD)json.size(), &written, nullptr) && written == json.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

}  // namespace status_file
