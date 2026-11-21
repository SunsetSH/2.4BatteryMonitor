#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace app_settings {

inline constexpr wchar_t kKey[] = L"Software\\2.4BatteryMonitor";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"2.4BatteryMonitor";
inline constexpr wchar_t kAppUserModelId[] = L"SunsetSH.BatteryMonitor24";

void Initialize();

// Values under HKCU\Software\2.4BatteryMonitor. With SetReadOnly(true) (`--demo`) nothing is written.
void SetReadOnly(bool readOnly);
bool LoadBool(const wchar_t* name, bool fallback);
void SaveBool(const wchar_t* name, bool on);
DWORD LoadDword(const wchar_t* name, DWORD fallback);
void SaveDword(const wchar_t* name, DWORD value);
std::wstring LoadText(const wchar_t* name, const std::wstring& fallback = L"");
void SaveText(const wchar_t* name, const std::wstring& value);
// REG_MULTI_SZ as a list of strings; at most `maxBytes` are read.
std::vector<std::wstring> LoadList(const wchar_t* name, DWORD maxBytes = 128 * 1024);
void SaveList(const wchar_t* name, const std::vector<std::wstring>& items);
void SaveRaw(const wchar_t* name, DWORD type, const void* data, DWORD size);

}  // namespace app_settings
