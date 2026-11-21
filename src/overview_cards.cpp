// The overview's card list: painting, scrolling, keyboard, mouse and drag to reorder. See overview.h.
#include "overview.h"

#include <windowsx.h>

#include <algorithm>

#include "ui_theme.h"

namespace overview::cards {

namespace {

constexpr wchar_t kClass[] = L"24BatteryMonitorCards";
constexpr int kCardHeight = 66, kGap = 6, kRadius = 8;

struct State {
    Messages messages{};
    HWND notify = nullptr;
    std::vector<Row> rows;
    int selected = -1, hover = -1;
    int scroll = 0;  // pixels scrolled from the top
    bool tracking = false;
    int pressed = -1, dragTo = -1, pressY = 0;
    bool dragging = false;
    int thumbGrab = -1;  // while the scroll thumb is dragged: the mouse's offset into it
    HFONT name = nullptr, detail = nullptr, level = nullptr, icon = nullptr, mark = nullptr;
};

State* Get(HWND w) { return reinterpret_cast<State*>(GetWindowLongPtrW(w, GWLP_USERDATA)); }

int Step(HWND w) { return ui::Scale(w, kCardHeight + kGap); }

void DeleteFonts(State& s) {
    for (HFONT* f : { &s.name, &s.detail, &s.level, &s.icon, &s.mark })
        if (*f) {
            DeleteObject(*f);
            *f = nullptr;
        }
}

void MakeFonts(HWND w, State& s) {
    DeleteFonts(s);
    s.name = ui::CreateUiFont(w, 11, FW_SEMIBOLD);
    s.detail = ui::CreateUiFont(w, 9, FW_NORMAL);
    s.level = ui::CreateUiFont(w, 15, FW_SEMIBOLD);
    s.icon = ui::CreateIconFont(w, 24);
    s.mark = ui::CreateIconFont(w, 12);
}

int ContentHeight(HWND w, const State& s) { return (int)s.rows.size() * Step(w); }

int MaxScroll(HWND w, const State& s) {
    RECT r;
    GetClientRect(w, &r);
    return std::max(0, ContentHeight(w, s) - (int)r.bottom);
}

void UpdateScroll(HWND w, State& s) { s.scroll = std::clamp(s.scroll, 0, MaxScroll(w, s)); }

// A slim scroll thumb in the palette (the system scroll bar has no dark style): the right edge of the
// list, only when the cards do not fit.
int GutterWidth(HWND w, const State& s) { return MaxScroll(w, s) > 0 ? ui::Scale(w, 12) : 0; }

RECT Thumb(HWND w, const State& s) {
    RECT r;
    GetClientRect(w, &r);
    const int content = ContentHeight(w, s), maxScroll = MaxScroll(w, s);
    if (maxScroll <= 0 || content <= 0) return {};
    const int h = std::max(ui::Scale(w, 32), (int)((long long)r.bottom * r.bottom / content));
    const int top = (int)((long long)(r.bottom - h) * s.scroll / maxScroll);
    return { r.right - ui::Scale(w, 7), top, r.right - ui::Scale(w, 3), top + h };
}

RECT CardRect(HWND w, const State& s, int i) {
    RECT r;
    GetClientRect(w, &r);
    const int top = i * Step(w) - s.scroll;
    return { 0, top, r.right - GutterWidth(w, s), top + ui::Scale(w, kCardHeight) };
}

int HitTest(HWND w, const State& s, int y) {
    const int i = (y + s.scroll) / Step(w);
    if (i < 0 || i >= (int)s.rows.size()) return -1;
    return (y + s.scroll) - i * Step(w) < ui::Scale(w, kCardHeight) ? i : -1;
}

void EnsureVisible(HWND w, State& s, int i) {
    RECT client;
    GetClientRect(w, &client);
    RECT r = CardRect(w, s, i);
    if (r.top < 0)
        s.scroll += r.top;
    else if (r.bottom > client.bottom)
        s.scroll += r.bottom - client.bottom;
    UpdateScroll(w, s);
    InvalidateRect(w, nullptr, FALSE);
}

COLORREF ToneColor(const ui::Palette& p, const Row& row) {
    if (row.dim) return p.muted;
    if (row.tone == status_text::Level::Low) return p.low;
    if (row.tone == status_text::Level::Warn) return p.warn;
    return p.text;
}

void PaintCard(HWND w, HDC dc, const State& s, const ui::Palette& p, int i, RECT r) {
    const Row& row = s.rows[i];
    const bool focused = GetFocus() == w && i == s.selected;
    const COLORREF fill = (i == s.hover || i == s.pressed) ? p.surfaceHover : p.surface;
    ui::FillRound(dc, r, fill, focused || (s.dragging && i == s.pressed) ? p.accent : p.border, ui::Scale(w, kRadius));
    if (focused) {  // a second ring line: the keyboard focus must be clearly visible
        RECT in = r;
        InflateRect(&in, -1, -1);
        HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
        HPEN pen = CreatePen(PS_SOLID, 1, p.accent);
        HGDIOBJ oldP = SelectObject(dc, pen);
        RoundRect(dc, in.left, in.top, in.right, in.bottom, ui::Scale(w, kRadius), ui::Scale(w, kRadius));
        SelectObject(dc, oldP);
        SelectObject(dc, oldB);
        DeleteObject(pen);
    }
    const int pad = ui::Scale(w, 16);
    const COLORREF main = row.dim ? p.muted : p.text;

    HGDIOBJ old = SelectObject(dc, s.icon);
    ui::DrawLabel(dc, std::wstring(1, ui::DeviceGlyph(row.kind, row.bluetooth)),
                  { r.left + pad, r.top, r.left + pad + ui::Scale(w, 28), r.bottom }, main,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // Right column: level, the charging / full mark, and the level bar under them.
    const int rightW = ui::Scale(w, 112);
    RECT levelBox{ r.right - pad - rightW, r.top + ui::Scale(w, 9), r.right - pad, r.top + ui::Scale(w, 42) };
    SelectObject(dc, s.level);
    SIZE textSize{};
    GetTextExtentPoint32W(dc, row.level.c_str(), (int)row.level.size(), &textSize);
    ui::DrawLabel(dc, row.level, levelBox, ToneColor(p, row), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    if (row.charging || row.full) {
        SelectObject(dc, s.mark);
        RECT markBox{ levelBox.left, levelBox.top, levelBox.right - textSize.cx - ui::Scale(w, 6), levelBox.bottom };
        ui::DrawLabel(dc, std::wstring(1, row.full ? ui::kGlyphCheck : ui::kGlyphBolt), markBox,
                      row.full ? p.level : p.charge, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
    if (row.percent >= 0) {
        const int barH = std::max(3, ui::Scale(w, 4)), barW = ui::Scale(w, 96);
        RECT track{ r.right - pad - barW, r.top + ui::Scale(w, 46), r.right - pad, r.top + ui::Scale(w, 46) + barH };
        ui::FillRound(dc, track, p.track, p.track, barH);
        COLORREF c = row.charging                           ? p.charge
                     : row.tone == status_text::Level::Low  ? p.low
                     : row.tone == status_text::Level::Warn ? p.warn
                                                            : p.level;
        if (row.dim) c = ui::Blend(c, p.surface, 110);
        RECT fillR = track;
        fillR.right = track.left + std::max(row.percent > 0 ? barH : 0, barW * std::min(row.percent, 100) / 100);
        if (fillR.right > fillR.left) ui::FillRound(dc, fillR, c, c, barH);
    }

    RECT text{ r.left + pad + ui::Scale(w, 42), r.top, r.right - pad - rightW - ui::Scale(w, 12), r.bottom };
    SelectObject(dc, s.name);
    ui::DrawLabel(dc, row.name, { text.left, r.top + ui::Scale(w, 10), text.right, r.top + ui::Scale(w, 34) }, main);
    SelectObject(dc, s.detail);
    ui::DrawLabel(dc, row.detail, { text.left, r.top + ui::Scale(w, 34), text.right, r.bottom - ui::Scale(w, 8) },
                  p.muted);
    SelectObject(dc, old);
}

void Paint(HWND w, State& s) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(w, &ps);
    RECT client;
    GetClientRect(w, &client);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, std::max<LONG>(1, client.right), std::max<LONG>(1, client.bottom));
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    const ui::Palette& p = ui::Colors(ui::AppsLight());
    HBRUSH bg = CreateSolidBrush(p.bg);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    for (int i = 0; i < (int)s.rows.size(); i++) {
        RECT r = CardRect(w, s, i);
        if (r.bottom < 0 || r.top > client.bottom) continue;
        PaintCard(w, dc, s, p, i, r);
    }
    if (RECT thumb = Thumb(w, s); thumb.bottom > thumb.top)
        ui::FillRound(dc, thumb, s.thumbGrab >= 0 ? p.muted : p.border, s.thumbGrab >= 0 ? p.muted : p.border,
                      thumb.right - thumb.left);
    if (s.dragging && s.dragTo >= 0) {  // where the dragged card will land
        int y = s.dragTo * Step(w) - s.scroll - ui::Scale(w, kGap) / 2;
        if (s.dragTo > s.pressed) y = (s.dragTo + 1) * Step(w) - s.scroll - ui::Scale(w, kGap) / 2;
        RECT line{ ui::Scale(w, 8), y - 1, client.right - ui::Scale(w, 8), y + 1 };
        HBRUSH accent = CreateSolidBrush(p.accent);
        FillRect(dc, &line, accent);
        DeleteObject(accent);
    }
    BitBlt(screen, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(w, &ps);
}

void Select(HWND w, State& s, int i) {
    if (s.rows.empty()) return;
    s.selected = std::clamp(i, 0, (int)s.rows.size() - 1);
    EnsureVisible(w, s, s.selected);
}

void PostMenu(HWND w, State& s, int i, POINT screen) {
    if (i < 0) return;
    PostMessageW(s.notify, s.messages.menu, (WPARAM)i, MAKELPARAM(screen.x, screen.y));
    (void)w;
}

bool OnKey(HWND w, State& s, WPARAM key) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const int page = std::max(1, (int)([&] {
                                  RECT r;
                                  GetClientRect(w, &r);
                                  return r.bottom;
                              }() / Step(w)));
    switch (key) {
    case VK_UP:
    case VK_DOWN: {
        const int dir = key == VK_UP ? -1 : 1;
        if (ctrl && s.selected >= 0) {
            const int to = s.selected + dir;
            if (to >= 0 && to < (int)s.rows.size()) {
                    // The focus follows the moved device: SetRows() finds it again by its key.
                PostMessageW(s.notify, s.messages.move, (WPARAM)s.selected, (LPARAM)to);
            }
        } else {
            Select(w, s, s.selected < 0 ? 0 : s.selected + dir);
        }
        return true;
    }
    case VK_PRIOR:
        Select(w, s, s.selected - page);
        return true;
    case VK_NEXT:
        Select(w, s, s.selected + page);
        return true;
    case VK_HOME:
        Select(w, s, 0);
        return true;
    case VK_END:
        Select(w, s, (int)s.rows.size() - 1);
        return true;
    case VK_RETURN:
        if (s.selected >= 0) PostMessageW(s.notify, s.messages.history, (WPARAM)s.selected, 0);
        return true;
    }
    return false;
}

void ShowMenuForSelection(HWND w, State& s) {
    if (s.selected < 0) Select(w, s, 0);
    if (s.selected < 0) return;
    RECT r = CardRect(w, s, s.selected);
    POINT pt{ r.left + ui::Scale(w, 48), r.bottom };
    ClientToScreen(w, &pt);
    PostMenu(w, s, s.selected, pt);
}

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = Get(w);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR) reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        break;
    case WM_NCDESTROY:
        if (s) {
            DeleteFonts(*s);
            delete s;
        }
        SetWindowLongPtrW(w, GWLP_USERDATA, 0);
        break;
    case WM_PAINT:
        if (s) Paint(w, *s);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (s) UpdateScroll(w, *s);
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_SETFOCUS:
        if (s && s->selected < 0 && !s->rows.empty()) s->selected = 0;
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_GETDLGCODE: {
            // Arrows and Enter belong to the list; Tab and Esc keep working for the dialog.
        LRESULT code = DLGC_WANTARROWS;
        auto* m = reinterpret_cast<MSG*>(lp);
        if (m && m->message == WM_KEYDOWN && m->wParam == VK_RETURN) code |= DLGC_WANTMESSAGE;
        return code;
    }
    case WM_KEYDOWN:
        if (s && OnKey(w, *s, wp)) return 0;
        break;
    case WM_CONTEXTMENU:
        if (!s) break;
        if (lp == -1) ShowMenuForSelection(w, *s);  // Shift+F10 or the Menu key
        return 0;
    case WM_MOUSEWHEEL:
        if (!s) break;
        s->scroll -= GET_WHEEL_DELTA_WPARAM(wp) * Step(w) / WHEEL_DELTA;
        UpdateScroll(w, *s);
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!s) break;
        if (!s->tracking) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, w, 0 };
            s->tracking = TrackMouseEvent(&t);
        }
        const int y = GET_Y_LPARAM(lp);
        if (s->thumbGrab >= 0 && GetCapture() == w) {
            RECT r, thumb = Thumb(w, *s);
            GetClientRect(w, &r);
            const int track = r.bottom - (thumb.bottom - thumb.top);
            if (track > 0) s->scroll = (int)((long long)(y - s->thumbGrab) * MaxScroll(w, *s) / track);
            UpdateScroll(w, *s);
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        if (s->pressed >= 0 && GetCapture() == w) {
            if (!s->dragging && std::abs(y - s->pressY) > ui::Scale(w, 6)) s->dragging = true;
            if (s->dragging) {
                s->dragTo = std::clamp((y + s->scroll) / Step(w), 0, (int)s->rows.size() - 1);
                InvalidateRect(w, nullptr, FALSE);
            }
            return 0;
        }
        const int hit = HitTest(w, *s, y);
        if (hit != s->hover) {
            s->hover = hit;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (s) {
            s->tracking = false;
            s->hover = -1;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (!s) break;
        SetFocus(w);
        if (GutterWidth(w, *s) && GET_X_LPARAM(lp) >= CardRect(w, *s, 0).right) {  // the scroll gutter
            RECT thumb = Thumb(w, *s);
            const int y = GET_Y_LPARAM(lp);
            if (y < thumb.top || y >= thumb.bottom) {  // page towards the click
                RECT r;
                GetClientRect(w, &r);
                s->scroll += y < thumb.top ? -(int)r.bottom : (int)r.bottom;
                UpdateScroll(w, *s);
            } else {
                s->thumbGrab = y - thumb.top;
                SetCapture(w);
            }
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        s->pressed = HitTest(w, *s, GET_Y_LPARAM(lp));
        s->pressY = GET_Y_LPARAM(lp);
        s->dragging = false;
        s->dragTo = -1;
        if (s->pressed >= 0) {
            s->selected = s->pressed;
            SetCapture(w);
        }
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        if (s && s->thumbGrab >= 0) {
            s->thumbGrab = -1;
            ReleaseCapture();
            InvalidateRect(w, nullptr, FALSE);
            return 0;
        }
        if (!s || s->pressed < 0) break;
        const int from = s->pressed, to = s->dragTo;
        const bool dragged = s->dragging;
        s->pressed = s->dragTo = -1;
        s->dragging = false;
        ReleaseCapture();
        if (dragged && to >= 0 && to != from) {
            PostMessageW(s->notify, s->messages.move, (WPARAM)from, (LPARAM)to);
        } else if (!dragged && HitTest(w, *s, GET_Y_LPARAM(lp)) == from) {
            PostMessageW(s->notify, s->messages.history, (WPARAM)from, 0);
        }
        InvalidateRect(w, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (s && (HWND)lp != w) s->thumbGrab = -1;
        if (s && (HWND)lp != w && s->pressed >= 0) {
            s->pressed = s->dragTo = -1;
            s->dragging = false;
            InvalidateRect(w, nullptr, FALSE);
        }
        return 0;
    case WM_RBUTTONUP: {
        if (!s) break;
        const int hit = HitTest(w, *s, GET_Y_LPARAM(lp));
        if (hit < 0) return 0;
        s->selected = hit;
        SetFocus(w);
        InvalidateRect(w, nullptr, FALSE);
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(w, &pt);
        PostMenu(w, *s, hit, pt);
        return 0;
    }
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

HWND Create(HWND parent, HINSTANCE instance, const Messages& messages, HWND notify) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        if (!registered) return nullptr;
    }
    auto* state = new State;
    state->messages = messages;
    state->notify = notify;
    HWND w = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, parent, nullptr, instance,
                             state);
    if (!w) {
        delete state;
        return nullptr;
    }
    MakeFonts(w, *state);
    return w;
}

void SetRows(HWND list, const std::vector<Row>& rows) {
    State* s = list ? Get(list) : nullptr;
    if (!s) return;
    // Keep the focus on the same device when the order or the set changes.
    const std::wstring key = s->selected >= 0 && s->selected < (int)s->rows.size() ? s->rows[s->selected].key : L"";
    s->rows = rows;
    s->selected = -1;
    for (int i = 0; i < (int)rows.size(); i++)
        if (rows[i].key == key) s->selected = i;
    if (s->hover >= (int)rows.size()) s->hover = -1;
    UpdateScroll(list, *s);
    InvalidateRect(list, nullptr, FALSE);
}

void Refresh(HWND list) {
    State* s = list ? Get(list) : nullptr;
    if (!s) return;
    MakeFonts(list, *s);
    UpdateScroll(list, *s);
    InvalidateRect(list, nullptr, FALSE);
}

}  // namespace overview::cards
