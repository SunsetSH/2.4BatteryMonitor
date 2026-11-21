#pragma once
// Battery icons for the notification area: the level as large digits over a thin level bar.
#include <windows.h>

#include <string>

#include "brands.h"
#include "status_text.h"

namespace tray_icons {

// `light`: the taskbar is light (SystemUsesLightTheme). The caller destroys the icon.
HICON Render(const std::wstring& text, int barPct, COLORREF textColor, COLORREF barColor, bool dim, bool light);
// A device's icon: digits in the taskbar color (red when low), bar in the state color (blue
// charging, red low, orange soon low, else the brand color or green), "×" when offline.
HICON ForDevice(const status_text::Shown& shown, status_text::Level tone, Accent accent, bool light);

}  // namespace tray_icons
