#include "update.h"
#include "app_settings.h"
#include "self_update.h"
#include "update_core.h"
#include "i18n.h"
#include "../build/version.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#include <atomic>
#include <memory>
#include <vector>

namespace updates {
namespace {

constexpr UINT kCheck = 470, kAutomatic = 471, kInstall = 472, kRelease = 473;
constexpr UINT_PTR kSchedule = 21, kCompletion = 22;
// The automatic check runs once per start (usually at sign-in), 30 s after it. Without a network yet, or with
// a full-screen app hiding the notification, it tries again a few times.
constexpr DWORD kFirstCheck = 30 * 1000, kRetry = 5 * 60 * 1000;
constexpr unsigned kAutomaticTries = 3;
constexpr auto kSettings = app_settings::kKey;
constexpr wchar_t kGitHub[] = L"github.com";
constexpr char kRepo[] = WB_UPDATE_REPO;
constexpr char kKey[] = WB_UPDATE_PUBLIC_KEY;
constexpr wchar_t kProject[] = L"/" WB_UPDATE_REPO_W L"/releases";
const Version kCurrent{WB_VERSION_MAJOR, WB_VERSION_MINOR, WB_VERSION_PATCH};

struct Internet {
    HINTERNET handle;
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
};

struct Response {
    DWORD status = 0;
    std::wstring location;
    std::vector<BYTE> bytes;
};

struct Job {
    enum class Kind { Manual, Automatic, Download } kind;
    bool portable = false;  // Download: the bare executable for a portable copy instead of the installer
    std::wstring version;
    std::vector<BYTE> installer;
    bool success = false;
    std::atomic<bool> cancelled{false};
};

HWND window;
bool wlmouseOnly;
bool automatic;
unsigned automaticTries;  // automatic checks started since this start
bool (*announce)(const std::wstring& version);
std::wstring available;
std::shared_ptr<Job> active;
HANDLE worker;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD);

bool Redirect(DWORD status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

Response Request(const wchar_t* host, const std::wstring& path, size_t limit, const Job& job) {
    if (job.cancelled) return {};
    constexpr DWORD features = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD disabled = features;
    Internet session{WinHttpOpen(L"2.4BatteryMonitor", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
    if (!session.handle || !WinHttpSetTimeouts(session.handle, 4000, 4000, 5000, 5000)) return {};
    Internet server{WinHttpConnect(session.handle, host, INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!server.handle) return {};
    Internet request{WinHttpOpenRequest(server.handle, L"GET", path.c_str(), nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)};
    if (!request.handle ||
        !WinHttpSetOption(request.handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSendRequest(request.handle, nullptr, 0, nullptr, 0, 0, 0) ||
        job.cancelled || !WinHttpReceiveResponse(request.handle, nullptr)) return {};
    Response response;
    DWORD size = sizeof(response.status);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             nullptr, &response.status, &size, nullptr)) return {};
    if (Redirect(response.status)) {
        wchar_t location[4096]{};
        size = sizeof(location);
        if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_LOCATION, nullptr, location, &size, nullptr)) return {};
        response.location = location;
    } else if (response.status == 200 && limit) {
        const ULONGLONG end = GetTickCount64() + 45000;
        BYTE buffer[32768];
        while (!job.cancelled && GetTickCount64() < end) {
            DWORD count = 0;
            if (!WinHttpReadData(request.handle, buffer, sizeof(buffer), &count) ||
                count > limit - response.bytes.size()) return {};
            if (!count) return response;
            response.bytes.insert(response.bytes.end(), buffer, buffer + count);
        }
        return {};
    }
    return response;
}

std::vector<BYTE> Asset(const std::wstring& version, const std::wstring& name, size_t limit, const Job& job) {
    auto response = Request(kGitHub, std::wstring(kProject) + L"/download/v" + version + L"/" + name, limit, job);
    if (!Redirect(response.status)) return std::move(response.bytes);
    URL_COMPONENTSW url{sizeof(url)};
    url.dwHostNameLength = url.dwUrlPathLength = url.dwExtraInfoLength =
        url.dwUserNameLength = url.dwPasswordLength = DWORD(-1);
    const auto& redirect = response.location;
    if (!WinHttpCrackUrl(redirect.c_str(), (DWORD)redirect.size(), 0, &url) ||
        url.nScheme != INTERNET_SCHEME_HTTPS || url.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        url.dwUserNameLength || url.dwPasswordLength || !url.dwHostNameLength || !url.dwUrlPathLength) return {};
    std::wstring host(url.lpszHostName, url.dwHostNameLength);
    if (!ReleaseAssetHost(host)) return {};
    std::wstring path(url.lpszUrlPath, url.dwUrlPathLength);
    if (url.dwExtraInfoLength) path.append(url.lpszExtraInfo, url.dwExtraInfoLength);
    return Request(host.c_str(), path, limit, job).bytes;
}

std::string Hex(const BYTE* bytes, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        out += "0123456789abcdef"[bytes[i] >> 4];
        out += "0123456789abcdef"[bytes[i] & 15];
    }
    return out;
}

std::vector<BYTE> PublicKey() {
    auto digit = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    std::vector<BYTE> out;
    for (size_t i = 0; i + 1 < sizeof(kKey) - 1; i += 2) {
        out.push_back((BYTE)((digit(kKey[i]) << 4) | digit(kKey[i + 1])));
    }
    return out;
}

bool Verified(std::vector<BYTE>& publicKey, const std::wstring& name, std::vector<BYTE>& bytes, std::vector<BYTE>& signature) {
    if (bytes.empty() || signature.size() != 64 || publicKey.empty()) return false;
    BCRYPT_ALG_HANDLE hash = nullptr, ec = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    BYTE digest[32]{};
    std::string payload;
    bool ok = BCryptOpenAlgorithmProvider(&hash, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptHash(hash, nullptr, 0, bytes.data(), (ULONG)bytes.size(), digest, sizeof(digest)) == 0;
    if (ok) {
        payload = SignedPayload(kRepo, std::string(name.begin(), name.end()), Hex(digest, sizeof(digest)));
        ok = BCryptHash(hash, nullptr, 0, (PUCHAR)payload.data(), (ULONG)payload.size(), digest, sizeof(digest)) == 0 &&
             BCryptOpenAlgorithmProvider(&ec, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0 &&
             BCryptImportKeyPair(ec, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, publicKey.data(), (ULONG)publicKey.size(), 0) == 0 &&
             BCryptVerifySignature(key, nullptr, digest, sizeof(digest), signature.data(), (ULONG)signature.size(), 0) == 0;
    }
    if (key) BCryptDestroyKey(key);
    if (ec) BCryptCloseAlgorithmProvider(ec, 0);
    if (hash) BCryptCloseAlgorithmProvider(hash, 0);
    return ok;
}

DWORD WINAPI Work(LPVOID parameter) {
    std::unique_ptr<std::shared_ptr<Job>> arg(static_cast<std::shared_ptr<Job>*>(parameter));
    Job& job = **arg;
    try {
        if (job.kind == Job::Kind::Download) {
            auto name = job.portable ? PortableAssetName(job.version) : SetupAssetName(job.version);
            auto signature = Asset(job.version, name + L".sig", 64, job);
            if (signature.size() == 64) {
                auto bytes = Asset(job.version, name, 64 * 1024 * 1024, job);
                auto publicKey = PublicKey();
                job.success = Verified(publicKey, name, bytes, signature);
                if (job.success) job.installer = std::move(bytes);
            }
        } else {
            auto result = Request(kGitHub, std::wstring(kProject) + L"/latest", 0, job);
            if (Redirect(result.status)) {
                job.version = ReleaseTag(result.location, WB_UPDATE_REPO_W);
                if (auto parsed = ParseVersion(job.version)) {
                    job.success = true;
                    if (*parsed <= kCurrent) job.version.clear();
                }
            }
        }
    } catch (...) {
        job.success = false;
    }
    return 0;
}

void Start(Job::Kind kind, bool portable = false) {
    if (!window || worker || !SetTimer(window, kCompletion, 250, Tick)) return;
    active = std::make_shared<Job>();
    active->kind = kind;
    active->portable = portable;
    if (kind == Job::Kind::Download) active->version = available;
    auto* arg = new std::shared_ptr<Job>(active);
    worker = CreateThread(nullptr, 0, Work, arg, 0, nullptr);
    if (!worker) {
        delete arg;
        active.reset();
        KillTimer(window, kCompletion);
    }
}

void OpenReleasePage() {
    if (available.empty()) return;
    auto page = std::wstring(L"https://github.com") + kProject + L"/tag/v" + available;
    ShellExecuteW(window, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Match the registered installation to the running executable before offering an in-place upgrade.
const wchar_t* InstallScope() {
    constexpr wchar_t uninstall[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}_is1";
    wchar_t exe[32768];
    DWORD n = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    if (!n || n >= ARRAYSIZE(exe)) return nullptr;
    for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (DWORD view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY}) {
            wchar_t location[32768]{};
            DWORD count = sizeof(location);
            if (RegGetValueW(hive, uninstall, L"InstallLocation", RRF_RT_REG_SZ | view, nullptr, location, &count) != ERROR_SUCCESS) continue;
            std::wstring installed(location);
            if (installed.empty()) continue;
            if (installed.back() != L'\\') installed += L'\\';
            if (_wcsicmp((installed + L"2.4BatteryMonitor.exe").c_str(), exe) == 0)
                return hive == HKEY_CURRENT_USER ? L"/CURRENTUSER" : L"/ALLUSERS";
        }
    }
    return nullptr;
}

bool RunSetup(const Job& job) {
    const wchar_t* scope = InstallScope();
    if (!scope) return false;
    wchar_t dir[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, dir))) return false;
    wchar_t unique[MAX_PATH]{};
    if (!GetTempFileNameW(dir, L"BHU", 0, unique)) return false;
    // A private staging directory keeps the executable's directory empty of other libraries.
    if (!DeleteFileW(unique) || !CreateDirectoryW(unique, nullptr)) return false;
    auto path = std::wstring(unique) + L"\\" + SetupAssetName(job.version);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { RemoveDirectoryW(unique); return false; }
    DWORD written = 0;
    bool ok = WriteFile(file, job.installer.data(), (DWORD)job.installer.size(), &written, nullptr) &&
              written == job.installer.size();
    CloseHandle(file);
    if (ok) {
        // Lock the file against replacement and verify the exact bytes again before starting it.
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (locked != INVALID_HANDLE_VALUE) {
            std::vector<BYTE> disk(job.installer.size());
            LARGE_INTEGER size{};
            DWORD read = 0;
            ok = GetFileSizeEx(locked, &size) && size.QuadPart == (LONGLONG)disk.size() &&
                 ReadFile(locked, disk.data(), (DWORD)disk.size(), &read, nullptr) &&
                 read == disk.size() && disk == job.installer;
            if (ok) {
                auto args = std::wstring(scope) + L" /WBUPDATE=1 /NORESTART";
                if (wlmouseOnly) args += L" /WLMOUSE=1";
                SHELLEXECUTEINFOW launch{sizeof(launch)};
                launch.hwnd = window;
                launch.lpFile = path.c_str();
                launch.lpParameters = args.c_str();
                launch.lpDirectory = unique;
                launch.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
                launch.nShow = SW_SHOWNORMAL;
                ok = ShellExecuteExW(&launch);
            }
            CloseHandle(locked);
        } else ok = false;
    }
    if (!ok) { DeleteFileW(path.c_str()); RemoveDirectoryW(unique); }
    return ok;
}

void CALLBACK Tick(HWND, UINT, UINT_PTR timer, DWORD) {
    if (timer == kSchedule) {
        if (!window) return;
        KillTimer(window, kSchedule);
        if (!automatic) return;
        if (worker) {  // a manual check or a download is running: look again a bit later
            SetTimer(window, kSchedule, kFirstCheck, Tick);
            return;
        }
        automaticTries++;
        Start(Job::Kind::Automatic);
        return;
    }
    if (!worker || WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return;
    KillTimer(window, kCompletion);
    CloseHandle(worker);
    worker = nullptr;
    auto done = std::move(active);
    if (done->kind == Job::Kind::Download) {
        if (done->success && done->portable) {
            // The new copy waits for this process to exit before it starts its tray.
            if (self_update::ReplaceAndRestart(done->installer, wlmouseOnly)) {
                PostMessageW(window, WM_CLOSE, 0, 0);
                return;
            }
            MessageBoxW(window, T(L"Could not update this copy in its folder. The release page opens instead."),
                        L"2.4BatteryMonitor", MB_OK | MB_ICONWARNING);
            OpenReleasePage();
        } else if (!done->success || !RunSetup(*done)) {
            MessageBoxW(window, T(L"The signed update could not be verified or started."), L"2.4BatteryMonitor", MB_OK | MB_ICONWARNING);
            if (done->portable) OpenReleasePage();  // e.g. a release without the signed portable executable
        }
    } else {
        if (done->success) available = done->version;
        // A newer version is announced on every start until it is installed: a Windows notification, nothing
        // is downloaded until the user agrees.
        if (done->kind == Job::Kind::Automatic) {
            const bool handled = done->success && (available.empty() || (announce && announce(available)));
            if (!handled && automatic && automaticTries < kAutomaticTries) SetTimer(window, kSchedule, kRetry, Tick);
        }
        if (done->kind == Job::Kind::Manual) {
            auto message = !done->success ? std::wstring(T(L"Could not check GitHub Releases."))
                : available.empty() ? std::wstring(T(L"2.4BatteryMonitor is up to date."))
                : TF(L"Version {version} is available. Open the Updates menu to continue.", {{L"version", available}});
            MessageBoxW(window, message.c_str(), L"2.4BatteryMonitor", MB_OK | MB_ICONINFORMATION);
        }
    }
}

}

void SetWlmouseOnly(bool only) { wlmouseOnly = only; }

void Initialize(HWND owner, bool only, bool (*announceVersion)(const std::wstring& version)) {
    window = owner;
    wlmouseOnly = only;
    announce = announceVersion;
    // On unless the user turned it off: checking only reads the latest release tag, installing always asks.
    automatic = app_settings::LoadDword(L"AutoCheckUpdates", 1) != 0;
    if (automatic) SetTimer(window, kSchedule, kFirstCheck, Tick);
}

void InstallAvailable() {
    if (!window || worker || available.empty()) return;
    if (!kKey[0]) return OpenReleasePage();  // a build without the release key cannot verify downloads
    const bool portable = !InstallScope();
    const std::wstring question =
        portable ? TF(L"Download version {version}, replace this copy and restart 2.4BatteryMonitor?",
                      { { L"version", available } })
                 : std::wstring(T(L"Download and start the signed 2.4BatteryMonitor installer?"));
    const int answer =
        MessageBoxW(window, question.c_str(), L"2.4BatteryMonitor", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer == IDYES) Start(Job::Kind::Download, portable);
}

void AddMenu(HMENU parent) {
    if (!window) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, worker ? MF_STRING | MF_GRAYED : MF_STRING, kCheck,
                T(worker ? L"Working on updates..." : L"Check for updates..."));
    AppendMenuW(menu, MF_STRING | (automatic ? MF_CHECKED : 0), kAutomatic, T(L"Automatically check for updates"));
    if (!available.empty()) {
        auto title = TF(L"Open release {version}", {{L"version", available}});
        AppendMenuW(menu, MF_STRING, kRelease, title.c_str());
        if (kKey[0]) {
            auto install = TF(L"Install version {version}...", {{L"version", available}});
            AppendMenuW(menu, MF_STRING | (worker ? MF_GRAYED : 0), kInstall, install.c_str());
        }
    }
    AppendMenuW(parent, MF_POPUP, (UINT_PTR)menu, T(L"Updates"));
}

bool MenuCommand(UINT command) {
    switch (command) {
        case kAutomatic: {
            automatic = !automatic;
            DWORD value = automatic;
            RegSetKeyValueW(HKEY_CURRENT_USER, kSettings, L"AutoCheckUpdates", REG_DWORD, &value, sizeof(value));
            automaticTries = 0;
            if (automatic) SetTimer(window, kSchedule, 1000, Tick);
            else KillTimer(window, kSchedule);
            return true;
        }
        case kCheck: Start(Job::Kind::Manual); return true;
        case kRelease:
            OpenReleasePage();
            return true;
        case kInstall:
            InstallAvailable();
            return true;
        default: return false;
    }
}

void Stop() {
    if (!window) return;
    KillTimer(window, kSchedule);
    KillTimer(window, kCompletion);
    if (active) active->cancelled = true;
    if (worker) CloseHandle(worker);
    worker = nullptr;
    active.reset();
    window = nullptr;
}

}
