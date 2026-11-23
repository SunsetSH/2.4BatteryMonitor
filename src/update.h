#pragma once
#include <windows.h>

#include <string>

namespace updates {

// `announce` shows "version X is available" when the automatic check (once per start) finds a newer
// version, and returns false when it could not be shown now (full-screen app): the check retries later.
void Initialize(HWND window, bool wlmouseOnly, bool (*announce)(const std::wstring& version));
void SetWlmouseOnly(bool only);  // "All supported devices" chosen: restart after an update in that mode
void AddMenu(HMENU parent);
bool MenuCommand(UINT command);
// Asks, then downloads and installs the available version: the installer for an installed copy, the
// executable in place for a portable one (self_update.h).
void InstallAvailable();
void Stop();

}
