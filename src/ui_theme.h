#pragma once
// The app's one palette and the theme rules (docs/DESIGN.md).
//  * Windows of the app (overview, history, settings, rename) follow the app theme, AppsUseLightTheme.
//  * Everything on the taskbar (widget, tray icons, their menu) follows SystemUsesLightTheme.
//  * A menu opened from a window follows that window.
// Meaning of colors everywhere: red low, orange soon low, blue charging, green level / fully charged.
#include <windows.h>

#include <string>

#include "devices.h"

namespace ui {

struct Palette {
    COLORREF bg;           // window background
    COLORREF surface;      // cards, panels, buttons
    COLORREF surfaceHover;
    COLORREF surfaceDown;
    COLORREF selected;     // the active choice of a button group
    COLORREF inset;        // chart area inside a panel
    COLORREF border;
    COLORREF grid;
    COLORREF text;
    COLORREF muted;        // secondary text
    COLORREF disabled;
    COLORREF accent;       // focus ring, links
    COLORREF level;        // battery level (bars, chart line); also "fully charged"
    COLORREF warn;         // within 15 points of the low threshold
    COLORREF low;          // at or below the low threshold
    COLORREF charge;       // charging
    COLORREF track;        // empty part of a level bar
};

const Palette& Colors(bool light);

bool AppsLight();     // HKCU ...\Themes\Personalize\AppsUseLightTheme (windows)
bool TaskbarLight();  // ...\SystemUsesLightTheme (taskbar, tray icons, their menu)

// Dark or light title bar and, on Windows 11, a caption in the window's background color.
void ApplyWindowFrame(HWND w, bool light);
// Light or dark popup menus for this process. Uses uxtheme ordinals 135 / 136, see docs/adr/0006.
void ApplyMenuTheme(bool light);

int Scale(HWND w, int px);  // px at 96 DPI -> the window's DPI
// Segoe UI Variable Text (Windows 11), else Segoe UI, at `pt` points for the window's DPI.
HFONT CreateUiFont(HWND w, int pt, int weight);
// Segoe Fluent Icons (Windows 11), else Segoe MDL2 Assets, `px` pixels at 96 DPI.
HFONT CreateIconFont(HWND w, int px);

void FillRound(HDC dc, const RECT& r, COLORREF fill, COLORREF outline, int radius);
void DrawLabel(HDC dc, const std::wstring& text, RECT r, COLORREF color,
               UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
COLORREF Blend(COLORREF a, COLORREF b, int alpha);  // alpha 0 = a, 255 = b

// Icon font glyphs shared by the widget and the windows.
wchar_t DeviceGlyph(DeviceKind k, bool bluetooth);
constexpr wchar_t kGlyphBolt = 0xE945;            // LightningBolt
constexpr wchar_t kGlyphCheck = 0xE73E;           // CheckMark, in place of the bolt once fully charged
constexpr wchar_t kGlyphBatteryUnknown = 0xE996;  // nothing to display

}  // namespace ui
