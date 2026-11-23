// Windows lets a running executable be renamed but not overwritten. So the update writes the new file
// next to it as "<exe>.new", renames the running one to "<exe>.old", moves the new one to the original
// name and starts it. The new process waits for the old one to exit and deletes "<exe>.old". The path
// stays the same, so the autostart entry and pinned shortcuts keep working.
#include "self_update.h"

#include <string>

namespace self_update {
namespace {

std::wstring ExePath() {
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    return n && n < ARRAYSIZE(buf) ? std::wstring(buf, n) : std::wstring();
}

bool WriteAll(const std::wstring& path, const std::vector<BYTE>& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size() &&
                    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

bool Launch(const std::wstring& exe, bool wlmouseOnly) {
    std::wstring cmd = L"\"" + exe + L"\" --updated-from " + std::to_wstring(GetCurrentProcessId());
    if (wlmouseOnly) cmd += L" --wlmouse";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

}  // namespace

bool ReplaceAndRestart(const std::vector<BYTE>& image, bool wlmouseOnly) {
    const std::wstring exe = ExePath();
    if (exe.empty() || image.empty()) return false;
    const std::wstring fresh = exe + L".new", old = exe + L".old";
    if (!WriteAll(fresh, image)) return false;  // read-only folder: nothing has changed yet
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(fresh.c_str());
        return false;
    }
    if (MoveFileExW(fresh.c_str(), exe.c_str(), 0) && Launch(exe, wlmouseOnly)) return true;
    // Roll back: the new file (if it got the name) out of the way, the running copy under its own name.
    if (Exists(exe)) MoveFileExW(exe.c_str(), fresh.c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(old.c_str(), exe.c_str(), 0);
    DeleteFileW(fresh.c_str());
    return false;
}

void FinishUpdate(DWORD oldPid) {
    if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, oldPid)) {
        WaitForSingleObject(old, 60 * 1000);
        CloseHandle(old);
    }
    const std::wstring exe = ExePath();
    if (exe.empty()) return;
    // The file stays locked for a moment after the process exits (and while antivirus scans it).
    const std::wstring old = exe + L".old";
    for (int i = 0; i < 20 && Exists(old) && !DeleteFileW(old.c_str()); i++) Sleep(100);
}

void RemoveLeftover() {
    const std::wstring exe = ExePath();
    if (!exe.empty()) DeleteFileW((exe + L".old").c_str());
}

}  // namespace self_update
