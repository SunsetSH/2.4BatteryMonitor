// Portable self-update on real files: a copy of this test replaces itself while running, restarts with
// --updated-from, and the new process removes the old file. Also: a failed write leaves the copy as it was.
#include "../src/self_update.h"

#include <shellapi.h>

#include <cassert>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

std::wstring Self() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::vector<BYTE> ReadAll(const std::wstring& path) {
    std::vector<BYTE> data;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    data.resize((size_t)size.QuadPart);
    DWORD read = 0;
    ReadFile(f, data.data(), (DWORD)data.size(), &read, nullptr);
    CloseHandle(f);
    return data;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// The exit code, or -1 when the process did not start or end in time.
long Run(const std::wstring& exe, const std::wstring& args, DWORD waitMs) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, waitMs) != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, &code))
        code = (DWORD)-1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (long)code;
}

}  // namespace

int main() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const std::wstring self = Self();
    const std::wstring marker = self + L".done";
    // The copy that replaced itself and restarted: finish like the app does, then report.
    if (argc == 3 && !wcscmp(argv[1], L"--updated-from")) {
        self_update::FinishUpdate(wcstoul(argv[2], nullptr, 10));
        HANDLE f = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        return 0;
    }
    // The running copy: replace itself with its own bytes (any valid executable works) and exit.
    if (argc == 2 && !wcscmp(argv[1], L"--replace"))
        return self_update::ReplaceAndRestart(ReadAll(self), false) ? 0 : 1;

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"bm-self-update-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring copy = dir + L"\\app.exe";
    assert(CopyFileW(self.c_str(), copy.c_str(), FALSE));

    // 1. Replace and restart: the new process waits for the old one, deletes app.exe.old and reports.
    assert(Run(copy, L"--replace", 10000) == 0);
    for (int i = 0; i < 100 && !Exists(copy + L".done"); i++) Sleep(100);
    assert(Exists(copy + L".done"));
    assert(Exists(copy) && !Exists(copy + L".old") && !Exists(copy + L".new"));
    assert(ReadAll(copy) == ReadAll(self));

    // 2. The new file cannot be written ("app.exe.new" is a directory): nothing changes.
    DeleteFileW((copy + L".done").c_str());
    assert(CreateDirectoryW((copy + L".new").c_str(), nullptr));
    assert(Run(copy, L"--replace", 10000) == 1);
    Sleep(500);
    assert(Exists(copy) && !Exists(copy + L".old") && !Exists(copy + L".done"));
    RemoveDirectoryW((copy + L".new").c_str());

    // 3. A leftover app.exe.old goes on the next start.
    const std::wstring leftover = self + L".old";
    assert(CopyFileW(self.c_str(), leftover.c_str(), FALSE));
    self_update::RemoveLeftover();
    assert(!Exists(leftover));

    DeleteFileW(copy.c_str());
    RemoveDirectoryW(dir.c_str());
    std::printf("self_update_test passed (replace and restart, failed write, leftover)\n");
    return 0;
}
