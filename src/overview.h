#pragma once
// Device overview: one card per device with its type, name, level bar and state, drawn in the app's
// palette (ui_theme.h). Clicking a card opens its battery history, right-clicking opens its menu, and
// cards can be dragged (or moved with Ctrl+Up / Ctrl+Down) to change the device order.
#include <windows.h>

#include <string>
#include <vector>

#include "devices.h"
#include "status_text.h"

namespace overview {

struct Row {
    std::wstring key;
    std::wstring name;
    std::wstring level;   // "47%", a coarse level ("Low"), "×" when offline, "?" when unknown
    std::wstring detail;  // second line: connection, state, earbud levels, time to empty
    DeviceKind kind = DeviceKind::Other;
    bool bluetooth = false;
    int percent = -1;     // level bar; -1 draws none
    bool charging = false;
    bool full = false;
    bool dim = false;
    status_text::Level tone = status_text::Level::Normal;

    bool operator==(const Row& o) const {
        return key == o.key && name == o.name && level == o.level && detail == o.detail && kind == o.kind &&
               bluetooth == o.bluetooth && percent == o.percent && charging == o.charging && full == o.full &&
               dim == o.dim && tone == o.tone;
    }
};

// Messages posted to the owner window.
struct Messages {
    UINT refresh;   // "Refresh now"
    UINT settings;  // "Settings"
    UINT close;     // the window was closed
    UINT history;   // wParam: row index
    UINT menu;      // wParam: row index, lParam: MAKELPARAM(screen x, screen y)
    UINT move;      // wParam: from index, lParam: to index
    UINT report;    // "Report a missing device"
};

// Shows the overview, creating it if needed. With `anchor` (a screen point on the taskbar) a newly
// shown window opens above it like a flyout; otherwise where Windows places it.
bool Open(HINSTANCE instance, HWND owner, const Messages& messages, const POINT* anchor = nullptr);
HWND Window();
// `status`: a title line, then after a line break a detail line. `busy`: a poll is running; while it
// runs an empty list shows no "nothing found" hint.
void Update(const std::vector<Row>& rows, const std::wstring& status, bool busy);
std::wstring KeyAt(int index);  // key of a row from the last Update(), empty when out of range
void Translate();
void Destroy();

namespace cards {
// The card list control (overview_cards.cpp), a child of the overview window.
HWND Create(HWND parent, HINSTANCE instance, const Messages& messages, HWND notify);
void SetRows(HWND list, const std::vector<Row>& rows);
void Refresh(HWND list);  // theme or DPI changed
}  // namespace cards

}  // namespace overview
