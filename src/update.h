#pragma once
#include <windows.h>

namespace updates {

void Initialize(HWND window, bool wlmouseOnly);
void AddMenu(HMENU parent);
bool MenuCommand(UINT command);
void Stop();

}
