#include "app_settings.h"

namespace app_settings {

namespace {
bool g_readOnly = false;
}

void Initialize() {
    HKEY settings;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE,
                       nullptr, &settings, nullptr) == ERROR_SUCCESS)
        RegCloseKey(settings);

    wchar_t exe[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    if (!length || length >= ARRAYSIZE(exe)) return;
    const auto current = L"\"" + std::wstring(exe) + L"\"";
    const auto scoped = current + L" --wlmouse";
    wchar_t previous[MAX_PATH * 2]{};
    DWORD size = sizeof(previous);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, previous, &size) != ERROR_SUCCESS) return;
    std::wstring next = current + L" --background";
    if (_wcsicmp(previous, scoped.c_str()) == 0) next += L" --wlmouse";
    else if (_wcsicmp(previous, current.c_str()) != 0) return;
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, next.c_str(), (DWORD)((next.size() + 1) * sizeof(wchar_t)));
}

void SetReadOnly(bool readOnly) { g_readOnly = readOnly; }

void SaveRaw(const wchar_t* name, DWORD type, const void* data, DWORD size) {
    if (!g_readOnly) RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, type, data, size);
}

bool LoadBool(const wchar_t* name, bool fallback) { return LoadDword(name, fallback ? 1 : 0) != 0; }

void SaveBool(const wchar_t* name, bool on) { SaveDword(name, on ? 1 : 0); }

DWORD LoadDword(const wchar_t* name, DWORD fallback) {
    DWORD v = fallback, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS) return fallback;
    return v;
}

void SaveDword(const wchar_t* name, DWORD value) { SaveRaw(name, REG_DWORD, &value, sizeof(value)); }

std::wstring LoadText(const wchar_t* name, const std::wstring& fallback) {
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return fallback;
    return buf;
}

void SaveText(const wchar_t* name, const std::wstring& value) {
    SaveRaw(name, REG_SZ, value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

std::vector<std::wstring> LoadList(const wchar_t* name, DWORD maxBytes) {
    std::vector<std::wstring> out;
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < 2 * sizeof(wchar_t) || size > maxBytes)
        return out;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return out;
    // The extra zeros above terminate a value written without its final empty string.
    const size_t limit = size / sizeof(wchar_t);
    for (size_t i = 0; i < limit && buf[i];) {
        size_t end = i;
        while (end < limit && buf[end]) ++end;
        out.emplace_back(buf.data() + i, end - i);
        i = end + 1;
    }
    return out;
}

void SaveList(const wchar_t* name, const std::vector<std::wstring>& items) {
    std::wstring multi;
    for (const auto& item : items) {
        multi += item;
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    if (multi.size() == 1) multi.push_back(L'\0');
    SaveRaw(name, REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

}  // namespace app_settings
