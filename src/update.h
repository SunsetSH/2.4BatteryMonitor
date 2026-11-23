#pragma once
#include <windows.h>

namespace updates {

void Initialize(HWND window, bool wlmouseOnly);
void SetWlmouseOnly(bool only);  // "All supported devices" chosen: restart after an update in that mode
void AddMenu(HMENU parent);
bool MenuCommand(UINT command);
void Stop();

}
