// Settings window. See settings_window.h.
#include "settings_window.h"

#include <windowsx.h>

#include <algorithm>

#include "i18n.h"
#include "ui_theme.h"

namespace settings_window {

namespace {

constexpr wchar_t kClass[] = L"24BatteryMonitorSettings";
constexpr int kPad = 20, kTitle = 48, kHeading = 34, kRow = 58, kGap = 4;

HWND window;
Builder builder;
std::vector<Row> rows;
int selected = -1, hover = -1, scroll = 0;
bool tracking = false;
HFONT titleFont, headingFont, rowFont, detailFont;

int S(int px) { return ui::Scale(window, px); }

void MakeFonts() {
    for (HFONT* f : { &titleFont, &headingFont, &rowFont, &detailFont })
        if (*f) DeleteObject(*f);
    titleFont = ui::CreateUiFont(window, 15, FW_SEMIBOLD);
    headingFont = ui::CreateUiFont(window, 10, FW_SEMIBOLD);
    rowFont = ui::CreateUiFont(window, 10, FW_NORMAL);
    detailFont = ui::CreateUiFont(window, 9, FW_NORMAL);
}

// Row rectangles in content coordinates (before scrolling).
std::vector<RECT> Layout(int width) {
    std::vector<RECT> out;
    int y = S(kPad + kTitle);
    for (const auto& r : rows) {
        if (!r.section.empty()) y += S(kHeading);
        out.push_back({ S(kPad), y, width - S(kPad), y + S(kRow) });
        y += S(kRow + kGap);
    }
    return out;
}

int ContentHeight(int width) {
    auto rects = Layout(width);
    return rects.empty() ? 0 : (int)rects.back().bottom + S(kPad);
}

void ClampScroll() {
    RECT c;
    GetClientRect(window, &c);
    scroll = std::clamp(scroll, 0, std::max(0, ContentHeight(c.right) - (int)c.bottom));
}

int HitTest(POINT pt) {
    RECT c;
    GetClientRect(window, &c);
    auto rects = Layout(c.right);
    for (int i = 0; i < (int)rects.size(); i++) {
        RECT r = rects[i];
        OffsetRect(&r, 0, -scroll);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

void Paint() {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(window, &ps);
    RECT c;
    GetClientRect(window, &c);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, std::max<LONG>(1, c.right), std::max<LONG>(1, c.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    const ui::Palette& p = ui::Colors(ui::AppsLight());
    HBRUSH bg = CreateSolidBrush(p.bg);
    FillRect(dc, &c, bg);
    DeleteObject(bg);
    HGDIOBJ oldFont = SelectObject(dc, titleFont);
    ui::DrawLabel(dc, T(L"Settings"), { S(kPad), S(kPad) - scroll, c.right - S(kPad), S(kPad + 32) - scroll }, p.text);
    auto rects = Layout(c.right);
    const bool focused = GetFocus() == window;
    for (int i = 0; i < (int)rows.size(); i++) {
        const Row& row = rows[i];
        RECT r = rects[i];
        OffsetRect(&r, 0, -scroll);
        if (r.bottom < 0 || r.top - S(kHeading) > c.bottom) continue;
        if (!row.section.empty()) {
            SelectObject(dc, headingFont);
            ui::DrawLabel(dc, row.section, { r.left + S(4), r.top - S(kHeading), r.right, r.top - S(6) }, p.muted,
                          DT_LEFT | DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS);
        }
        const bool sel = focused && i == selected;
        ui::FillRound(dc, r, i == hover && row.enabled ? p.surfaceHover : p.surface, sel ? p.accent : p.border, S(8));
        const int pad = S(16);
        const COLORREF text = row.enabled ? p.text : p.disabled;
        SelectObject(dc, rowFont);
        SIZE valueSize{};
        GetTextExtentPoint32W(dc, row.value.c_str(), (int)row.value.size(), &valueSize);
        const int valueW = std::min((int)valueSize.cx, (int)(r.right - r.left) / 2);
        const bool on = row.value == T(L"On");
        ui::DrawLabel(dc, row.value, { r.right - pad - valueW, r.top, r.right - pad, r.bottom },
                      !row.enabled ? p.disabled
                      : on         ? p.accent
                                   : p.muted,
                      DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        const int textRight = r.right - pad - valueW - S(16);
        const bool hasDetail = !row.detail.empty();
        ui::DrawLabel(dc, row.title,
                      { r.left + pad, r.top + (hasDetail ? S(8) : 0), textRight, hasDetail ? r.top + S(30) : r.bottom },
                      text);
        if (hasDetail) {
            SelectObject(dc, detailFont);
            ui::DrawLabel(dc, row.detail, { r.left + pad, r.top + S(30), textRight, r.bottom - S(6) }, p.muted);
        }
    }
    SelectObject(dc, oldFont);
    BitBlt(screen, 0, 0, c.right, c.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(window, &ps);
}

void EnsureVisible(int i) {
    RECT c;
    GetClientRect(window, &c);
    auto rects = Layout(c.right);
    if (i < 0 || i >= (int)rects.size()) return;
    const int top = rects[i].top - (rows[i].section.empty() ? 0 : S(kHeading)), bottom = rects[i].bottom;
    if (top - scroll < 0)
        scroll = top - S(kPad);
    else if (bottom - scroll > c.bottom)
        scroll = bottom - c.bottom + S(kPad);
    ClampScroll();
}

void Activate(int i) {
    if (i < 0 || i >= (int)rows.size() || !rows[i].enabled || !rows[i].activate) return;
    RECT c;
    GetClientRect(window, &c);
    RECT r = Layout(c.right)[i];
    POINT anchor{ r.right - S(16), r.bottom - scroll };
    ClientToScreen(window, &anchor);
    auto action = rows[i].activate;  // the rows may be rebuilt while it runs
    action(window, anchor);
    Refresh();
}

void Move(int to) {
    if (rows.empty()) return;
    selected = std::clamp(to, 0, (int)rows.size() - 1);
    EnsureVisible(selected);
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        const UINT dpi = GetDpiForWindow(w);
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize = { MulDiv(460, dpi, 96), MulDiv(360, dpi, 96) };
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        MakeFonts();
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && !lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet")) {
            ui::ApplyWindowFrame(w, ui::AppsLight());
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_SIZE:
        ClampScroll();
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        if (selected < 0 && !rows.empty()) selected = 0;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        scroll -= GET_WHEEL_DELTA_WPARAM(wp) * S(kRow) / WHEEL_DELTA;
        ClampScroll();
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, w, 0 };
            tracking = TrackMouseEvent(&t);
        }
        const int hit = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (hit != hover) {
            hover = hit;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking = false;
        hover = -1;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int hit = HitTest({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        if (hit >= 0) {
            selected = hit;
            Activate(hit);
        }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP:
            Move(selected - 1);
            return 0;
        case VK_DOWN:
            Move(selected < 0 ? 0 : selected + 1);
            return 0;
        case VK_HOME:
            Move(0);
            return 0;
        case VK_END:
            Move((int)rows.size() - 1);
            return 0;
        case VK_RETURN:
        case VK_SPACE:
            Activate(selected);
            return 0;
        case VK_ESCAPE:
            DestroyWindow(w);
            return 0;
        }
        break;
    case WM_DESTROY:
        for (HFONT* f : { &titleFont, &headingFont, &rowFont, &detailFont })
            if (*f) {
                DeleteObject(*f);
                *f = nullptr;
            }
        window = nullptr;
        rows.clear();
        selected = hover = -1;
        scroll = 0;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

void Open(HINSTANCE instance, Builder b) {
    builder = std::move(b);
    if (!window) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        const UINT dpi = GetDpiForSystem();
        const std::wstring title = std::wstring(T(L"Settings")) + L" - 2.4BatteryMonitor";
        window = CreateWindowExW(0, kClass, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 MulDiv(600, dpi, 96), MulDiv(680, dpi, 96), nullptr, nullptr, instance, nullptr);
        if (!window) return;
        ui::ApplyWindowFrame(window, ui::AppsLight());
        MakeFonts();
    }
    Refresh();
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    SetFocus(window);
}

void Refresh() {
    if (!window || !builder) return;
    rows = builder();
    if (selected >= (int)rows.size()) selected = (int)rows.size() - 1;
    ClampScroll();
    InvalidateRect(window, nullptr, FALSE);
}

HWND Window() { return window; }

void Destroy() {
    if (window) DestroyWindow(window);
}

}  // namespace settings_window
