// Device overview window: status line, buttons and the card list. See overview.h.
#include "overview.h"

#include <algorithm>

#include "i18n.h"
#include "ui_theme.h"

namespace overview {
namespace {

constexpr wchar_t kClass[] = L"24BatteryMonitorOverview";
enum : int { kRefreshId = 101, kSettingsId, kReportId };

HWND window, list, refreshButton, settingsButton, reportButton;
Messages messages{};
HWND owner;
HFONT font, titleFont;
std::vector<Row> lastRows;
std::wstring lastStatus;
bool light = true;
bool checking = true;  // the first poll is still running: no "nothing found" hint yet

int Scale(int px) { return ui::Scale(window, px); }

int ButtonWidth(HWND button) {
    wchar_t text[128]{};
    GetWindowTextW(button, text, 128);
    HDC dc = GetDC(window);
    HGDIOBJ old = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SIZE size{};
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    ReleaseDC(window, dc);
    return std::max(Scale(96), (int)size.cx + Scale(32));
}

int HeaderHeight() { return Scale(20 + 44 + 14); }

void Layout() {
    RECT r{};
    GetClientRect(window, &r);
    const int pad = Scale(20), height = Scale(32);
    const int settingsW = ButtonWidth(settingsButton), refreshW = ButtonWidth(refreshButton);
    const int top = pad + Scale(6);  // centred on the two status lines
    MoveWindow(settingsButton, r.right - pad - settingsW, top, settingsW, height, TRUE);
    MoveWindow(refreshButton, r.right - pad - settingsW - Scale(8) - refreshW, top, refreshW, height, TRUE);
    MoveWindow(list, pad, HeaderHeight(), std::max(0, (int)r.right - 2 * pad),
               std::max(0, (int)r.bottom - HeaderHeight() - pad), TRUE);
    const int reportW = ButtonWidth(reportButton);
    MoveWindow(reportButton, (r.right - reportW) / 2, HeaderHeight() + Scale(96), reportW, height, TRUE);
    InvalidateRect(window, nullptr, TRUE);
}

void SetFonts() {
    if (font) DeleteObject(font);
    if (titleFont) DeleteObject(titleFont);
    font = ui::CreateUiFont(window, 10, FW_NORMAL);
    titleFont = ui::CreateUiFont(window, 11, FW_SEMIBOLD);
    for (HWND child : { refreshButton, settingsButton, reportButton })
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    cards::Refresh(list);
}

void ApplyTheme() {
    light = ui::AppsLight();
    ui::ApplyWindowFrame(window, light);
    InvalidateRect(window, nullptr, TRUE);
    for (HWND b : { refreshButton, settingsButton, reportButton }) InvalidateRect(b, nullptr, TRUE);
    cards::Refresh(list);
}

void ShowEmptyState() {
    const bool empty = lastRows.empty();
    ShowWindow(list, empty ? SW_HIDE : SW_SHOW);
    ShowWindow(reportButton, empty && !checking ? SW_SHOW : SW_HIDE);
}

void PaintWindow() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT r;
    GetClientRect(window, &r);
    const ui::Palette& p = ui::Colors(light);
    HBRUSH bg = CreateSolidBrush(p.bg);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    HGDIOBJ old = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    const int pad = Scale(20);
    RECT refresh{};
    GetWindowRect(refreshButton, &refresh);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&refresh), 2);
    // Status: the scope as a title, the count and the time of the last poll under it.
    const size_t nl = lastStatus.find(L'\n');
    const int right = refresh.left - Scale(12);
    SelectObject(dc, titleFont ? titleFont : font);
    ui::DrawLabel(dc, lastStatus.substr(0, nl), { pad, pad, right, pad + Scale(24) }, p.text);
    SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    if (nl != std::wstring::npos)
        ui::DrawLabel(dc, lastStatus.substr(nl + 1), { pad, pad + Scale(24), right, pad + Scale(44) }, p.muted);
    if (lastRows.empty() && !checking) {
        RECT hint{ pad, HeaderHeight() + Scale(16), r.right - pad, HeaderHeight() + Scale(88) };
        ui::DrawLabel(dc, T(L"No devices with battery info found"),
                      { hint.left, hint.top, hint.right, hint.top + Scale(28) }, p.text,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        ui::DrawLabel(
            dc,
            T(L"Connect the receiver or pair the device. If it is still missing, report it so it can be supported."),
            { hint.left, hint.top + Scale(30), hint.right, hint.bottom }, p.muted, DT_CENTER | DT_WORDBREAK);
    }
    SelectObject(dc, old);
    EndPaint(window, &ps);
}

void DrawButton(const DRAWITEMSTRUCT* item) {
    const ui::Palette& p = ui::Colors(light);
    const bool down = item->itemState & ODS_SELECTED, focused = item->itemState & ODS_FOCUS;
    HBRUSH corners = CreateSolidBrush(p.bg);
    FillRect(item->hDC, &item->rcItem, corners);
    DeleteObject(corners);
    ui::FillRound(item->hDC, item->rcItem, down ? p.surfaceDown : p.surface, focused ? p.accent : p.border, Scale(8));
    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, 128);
    HGDIOBJ old = SelectObject(item->hDC, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    ui::DrawLabel(item->hDC, text, item->rcItem, p.text, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(item->hDC, old);
}

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        const UINT dpi = GetDpiForWindow(h);
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = { MulDiv(420, dpi, 96), MulDiv(300, dpi, 96) };
        return 0;
    }
    case WM_SIZE:
        if (list) Layout();
        return 0;
    case WM_DPICHANGED: {
        const auto* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetFonts();
        Layout();
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && !lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet")) ApplyTheme();
        return 0;
    case WM_PAINT:
        PaintWindow();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        if (reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType == ODT_BUTTON) {
            DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kRefreshId:
            PostMessageW(owner, messages.refresh, 0, 0);
            return 0;
        case kSettingsId:
            PostMessageW(owner, messages.settings, 0, 0);
            return 0;
        case kReportId:
            PostMessageW(owner, messages.report, 0, 0);
            return 0;
        case IDCANCEL:
            PostMessageW(owner, messages.close, 0, 0);
            return 0;  // Esc
        }
        return 0;
    case WM_CLOSE:
        PostMessageW(owner, messages.close, 0, 0);
        return 0;
    case WM_DESTROY:
        if (font) DeleteObject(font);
        if (titleFont) DeleteObject(titleFont);
        font = titleFont = nullptr;
        window = list = refreshButton = settingsButton = reportButton = nullptr;
        lastRows.clear();
        lastStatus.clear();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND MakeButton(HINSTANCE instance, int id) {
    return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window,
                           (HMENU)(INT_PTR)id, instance, nullptr);
}

bool CreateOverview(HINSTANCE instance) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = Proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const UINT dpi = GetDpiForSystem();
    window =
        CreateWindowExW(0, kClass, L"2.4BatteryMonitor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                        CW_USEDEFAULT, MulDiv(640, dpi, 96), MulDiv(520, dpi, 96), nullptr, nullptr, instance, nullptr);
    if (!window) return false;
    refreshButton = MakeButton(instance, kRefreshId);
    settingsButton = MakeButton(instance, kSettingsId);
    list = cards::Create(window, instance, messages, owner);
    reportButton = MakeButton(instance, kReportId);
    if (!refreshButton || !settingsButton || !list || !reportButton) {
        Destroy();
        return false;
    }
    ApplyTheme();
    SetFonts();
    Translate();
    ShowEmptyState();
    return true;
}

// Above the taskbar point, right-aligned to it, inside the monitor's work area.
void PlaceAbove(POINT anchor) {
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi)) return;
    RECT r;
    GetWindowRect(window, &r);
    const int w = r.right - r.left, h = r.bottom - r.top, margin = Scale(12);
    const RECT& work = mi.rcWork;
    int x = std::clamp((int)anchor.x - w + Scale(40), (int)work.left + margin,
                       std::max((int)work.left, (int)work.right - w - margin));
    int y = anchor.y >= work.bottom ? work.bottom - h - margin
            : anchor.y <= work.top  ? work.top + margin
                                    : work.bottom - h - margin;
    if (anchor.x <= work.left) x = work.left + margin;  // left taskbar
    SetWindowPos(window, nullptr, x, std::max((int)work.top, y), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

bool Open(HINSTANCE instance, HWND notify, const Messages& m, const POINT* anchor) {
    owner = notify;
    messages = m;
    const bool created = !window;
    if (created && !CreateOverview(instance)) return false;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    if (anchor && (created || !IsWindowVisible(window))) PlaceAbove(*anchor);
    // The first ShowWindow can inherit SW_HIDE from the launcher's STARTUPINFO.
    // Showing via SetWindowPos also works when a later launch reopens an autostarted instance.
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    if (!lastRows.empty()) SetFocus(list);
    return true;
}

HWND Window() { return window; }

void Update(const std::vector<Row>& rows, const std::wstring& status, bool busy) {
    if (!window) return;
    if (status == lastStatus && rows == lastRows && busy == checking) return;
    lastStatus = status;
    checking = busy;
    if (rows != lastRows) {
        lastRows = rows;
        cards::SetRows(list, rows);
    }
    ShowEmptyState();
    InvalidateRect(window, nullptr, TRUE);
}

std::wstring KeyAt(int index) { return index >= 0 && index < (int)lastRows.size() ? lastRows[index].key : L""; }

void Translate() {
    if (!window) return;
    SetWindowTextW(refreshButton, T(L"Refresh now"));
    SetWindowTextW(settingsButton, T(L"Settings"));
    SetWindowTextW(reportButton, T(L"Report a missing device..."));
    Layout();
}

void Destroy() {
    if (window) DestroyWindow(window);
}

}  // namespace overview
