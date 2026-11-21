#include "rename_dialog.h"
#include "device_aliases.h"
#include "i18n.h"
#include "ui_theme.h"

#include <algorithm>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"24BatteryMonitorRenameDialog";

struct EditorState {
    std::wstring initial;
    std::wstring result;
    HWND edit = nullptr;
    bool accepted = false;
    bool light = true;  // the app theme (AppsUseLightTheme), like the other windows
    HFONT font = nullptr;
    HBRUSH bg = nullptr, field = nullptr;
};

EditorState* State(HWND hwnd) {
    return reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetControlFont(HWND control, HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)(font ? font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

// Save is the accent button, Cancel a plain one, both in the shared palette.
void DrawButton(const DRAWITEMSTRUCT* item, const EditorState& state) {
    const ui::Palette& p = ui::Colors(state.light);
    const bool primary = item->CtlID == IDOK, down = item->itemState & ODS_SELECTED, focused = item->itemState & ODS_FOCUS;
    FillRect(item->hDC, &item->rcItem, state.bg);
    COLORREF fill = primary ? p.accent : down ? p.surfaceDown : p.surface;
    if (primary && down) fill = ui::Blend(p.accent, p.bg, 60);
    ui::FillRound(item->hDC, item->rcItem, fill, focused && !primary ? p.accent : primary ? fill : p.border,
                  ui::Scale(item->hwndItem, 8));
    wchar_t text[64]{};
    GetWindowTextW(item->hwndItem, text, 64);
    HGDIOBJ old = SelectObject(item->hDC, state.font);
    ui::DrawLabel(item->hDC, text, item->rcItem, primary ? p.bg : p.text, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(item->hDC, old);
    if (focused && primary) {  // keyboard focus on the accent button: an inner ring in the window color
        RECT ring = item->rcItem;
        InflateRect(&ring, -ui::Scale(item->hwndItem, 3), -ui::Scale(item->hwndItem, 3));
        HGDIOBJ oldBrush = SelectObject(item->hDC, GetStockObject(NULL_BRUSH));
        HPEN pen = CreatePen(PS_SOLID, 1, p.bg);
        HGDIOBJ oldPen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, ring.left, ring.top, ring.right, ring.bottom, ui::Scale(item->hwndItem, 6),
                  ui::Scale(item->hwndItem, 6));
        SelectObject(item->hDC, oldPen);
        SelectObject(item->hDC, oldBrush);
        DeleteObject(pen);
    }
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);  // it sets the window title
    }
    EditorState* state = State(hwnd);
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            const int dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };
            state->light = ui::AppsLight();
            const ui::Palette& pal = ui::Colors(state->light);
            ui::ApplyWindowFrame(hwnd, state->light);
            state->font = ui::CreateUiFont(hwnd, 10, FW_NORMAL);
            state->bg = CreateSolidBrush(pal.bg);
            state->field = CreateSolidBrush(pal.surface);
            HWND label = CreateWindowExW(0, L"STATIC", T(L"Device display name:"),
                                         WS_CHILD | WS_VISIBLE, scale(16), scale(16), scale(382), scale(22),
                                         hwnd, nullptr, inst, nullptr);
            // No 3D edge: the window paints a rounded field around the edit control.
            state->edit = CreateWindowExW(0, L"EDIT", state->initial.c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          scale(26), scale(48), scale(362), scale(22),
                                          hwnd, (HMENU)(INT_PTR)100, inst, nullptr);
            HWND save = CreateWindowExW(0, L"BUTTON", T(L"Save"),
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                        scale(208), scale(88), scale(92), scale(32),
                                        hwnd, (HMENU)(INT_PTR)IDOK, inst, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", T(L"Cancel"),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                          scale(306), scale(88), scale(92), scale(32),
                                          hwnd, (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);
            SetControlFont(label, state->font);
            SetControlFont(state->edit, state->font);
            SetControlFont(save, state->font);
            SetControlFont(cancel, state->font);
            SendMessageW(state->edit, EM_SETLIMITTEXT, aliases::kMaxNameLength, 0);
            SetFocus(state->edit);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) {  // the field's accent outline
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (LOWORD(wp) == IDOK) {
                const int length = GetWindowTextLengthW(state->edit);
                std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, 0);
                GetWindowTextW(state->edit, buffer.data(), (int)buffer.size());
                std::wstring name = aliases::Trim(buffer.data());
                if (!aliases::ValidName(name)) {
                    MessageBoxW(hwnd, T(L"Enter a name (1-64 characters)."), T(L"Rename device"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                state->result = std::move(name);
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_ERASEBKGND: {
            RECT r;
            GetClientRect(hwnd, &r);
            FillRect((HDC)wp, &r, state->bg);
            // The rounded field around the edit control.
            RECT field{ ui::Scale(hwnd, 16), ui::Scale(hwnd, 42), ui::Scale(hwnd, 398), ui::Scale(hwnd, 76) };
            const ui::Palette& p = ui::Colors(state->light);
            ui::FillRound((HDC)wp, field, p.surface, GetFocus() == state->edit ? p.accent : p.border, ui::Scale(hwnd, 8));
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const ui::Palette& p = ui::Colors(state->light);
            const bool edit = msg == WM_CTLCOLOREDIT;
            SetTextColor((HDC)wp, p.text);
            SetBkColor((HDC)wp, edit ? p.surface : p.bg);
            return (LRESULT)(edit ? state->field : state->bg);
        }
        case WM_DRAWITEM:
            DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lp), *state);
            return TRUE;
        case WM_DESTROY:
            if (state->font) DeleteObject(state->font);
            if (state->bg) DeleteObject(state->bg);
            if (state->field) DeleteObject(state->field);
            state->font = nullptr;
            state->bg = state->field = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

bool PromptDeviceName(HWND owner, const std::wstring& currentName, std::wstring& result) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpfnWndProc = EditorProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // WM_ERASEBKGND paints the palette background
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    const int dpi = owner ? GetDpiForWindow(owner) : 96;
    const int width = MulDiv(420, dpi, 96), height = MulDiv(172, dpi, 96);
    POINT p{};
    GetCursorPos(&p);
    HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT area{};
    if (GetMonitorInfoW(monitor, &mi)) area = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int x = area.left + ((area.right - area.left - width) / 2);
    int y = area.top + ((area.bottom - area.top - height) / 2);
    x = std::max(x, static_cast<int>(area.left));
    y = std::max(y, static_cast<int>(area.top));

    EditorState state;
    state.initial = currentName;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass,
                                   T(L"Rename device"), WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                   x, y, width, height, owner, nullptr, inst, &state);
    if (!dialog) return false;

    const bool ownerEnabled = owner && IsWindowEnabled(owner);
    if (ownerEnabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    if (state.edit) {
        SetFocus(state.edit);
        SendMessageW(state.edit, EM_SETSEL, 0, -1);
    }
    MSG msg{};
    while (IsWindow(dialog)) {
        BOOL code = GetMessageW(&msg, nullptr, 0, 0);
        if (code <= 0) {
            if (code == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (ownerEnabled) EnableWindow(owner, TRUE);
    if (state.accepted) result = std::move(state.result);
    return state.accepted;
}
