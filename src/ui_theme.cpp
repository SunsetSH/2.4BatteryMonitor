// The app's palette and theme helpers. See ui_theme.h.
#include "ui_theme.h"

namespace ui {

namespace {

// Neutral Windows 11 surfaces; the state colors are the ones the taskbar widget has always used.
constexpr Palette kDark = {
    RGB(32, 32, 32),   RGB(43, 43, 43),   RGB(50, 50, 50),    RGB(38, 38, 38),    RGB(29, 52, 66),    RGB(28, 28, 28),
    RGB(61, 61, 61),   RGB(52, 52, 52),   RGB(255, 255, 255), RGB(178, 178, 178), RGB(110, 110, 110), RGB(96, 205, 255),
    RGB(108, 203, 95), RGB(255, 170, 60), RGB(255, 90, 90),   RGB(96, 205, 255),  RGB(70, 70, 70),
};
constexpr Palette kLight = {
    RGB(243, 243, 243), RGB(255, 255, 255), RGB(249, 249, 249), RGB(237, 237, 237), RGB(224, 238, 250),
    RGB(250, 250, 250), RGB(222, 222, 222), RGB(232, 232, 232), RGB(26, 26, 26),    RGB(96, 96, 96),
    RGB(160, 160, 160), RGB(0, 95, 184),    RGB(16, 124, 16),   RGB(157, 93, 0),    RGB(196, 30, 40),
    RGB(0, 120, 212),   RGB(220, 220, 220),
};

bool ReadThemeFlag(const wchar_t* name, bool fallback) {
    DWORD v = fallback ? 1 : 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name,
                 RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}

bool FaceIs(HFONT f, const wchar_t* face) {
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ o = SelectObject(dc, f);
    wchar_t got[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, o);
    DeleteDC(dc);
    return !_wcsicmp(got, face);
}

// GDI silently substitutes a missing face, so the result is checked before falling back.
HFONT CreateFace(int px, int weight, const wchar_t* face, const wchar_t* fallback) {
    HFONT f = CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    if (f && FaceIs(f, face)) return f;
    if (f) DeleteObject(f);
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, fallback);
}

}  // namespace

const Palette& Colors(bool light) { return light ? kLight : kDark; }

bool AppsLight() { return ReadThemeFlag(L"AppsUseLightTheme", true); }
bool TaskbarLight() { return ReadThemeFlag(L"SystemUsesLightTheme", false); }

void ApplyWindowFrame(HWND w, bool light) {
    static HMODULE lib = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using AttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto set = lib ? reinterpret_cast<AttributeFn>(GetProcAddress(lib, "DwmSetWindowAttribute")) : nullptr;
    if (!set || !w) return;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 since Windows 10 20H1 and 19 on earlier builds.
    BOOL dark = !light;
    if (FAILED(set(w, 20, &dark, sizeof(dark)))) set(w, 19, &dark, sizeof(dark));
    // DWMWA_CAPTION_COLOR (35) and DWMWA_TEXT_COLOR (36), Windows 11: the caption blends into the window.
    // Windows 10 rejects them and keeps its standard caption.
    COLORREF caption = Colors(light).bg, text = Colors(light).text;
    set(w, 35, &caption, sizeof(caption));
    set(w, 36, &text, sizeof(text));
}

void ApplyMenuTheme(bool light) {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setMode = ux ? (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135)) : nullptr;
    static auto flush = ux ? (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(light ? 3 : 2);  // ForceLight : ForceDark
    if (flush) flush();
}

int Scale(HWND w, int px) {
    UINT dpi = w ? GetDpiForWindow(w) : 0;
    return MulDiv(px, dpi ? (int)dpi : (int)GetDpiForSystem(), 96);
}

HFONT CreateUiFont(HWND w, int pt, int weight) {
    UINT dpi = w ? GetDpiForWindow(w) : 0;
    const int px = MulDiv(pt, dpi ? (int)dpi : (int)GetDpiForSystem(), 72);
    return CreateFace(px, weight, L"Segoe UI Variable Text", L"Segoe UI");
}

HFONT CreateIconFont(HWND w, int px) {
    return CreateFace(Scale(w, px), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
}

void FillRound(HDC dc, const RECT& r, COLORREF fill, COLORREF outline, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawLabel(HDC dc, const std::wstring& text, RECT r, COLORREF color, UINT flags) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, flags | DT_NOPREFIX);
}

COLORREF Blend(COLORREF a, COLORREF b, int alpha) {
    auto mix = [&](int x, int y) { return (x * (255 - alpha) + y * alpha) / 255; };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

wchar_t DeviceGlyph(DeviceKind k, bool bluetooth) {
    switch (k) {
    case DeviceKind::Headset:
        return 0xE7F6;  // Headphone
    case DeviceKind::Mouse:
        return 0xE962;  // Mouse
    case DeviceKind::Keyboard:
        return 0xE765;  // KeyboardClassic
    case DeviceKind::Gamepad:
        return 0xE7FC;  // Game
    case DeviceKind::Phone:
        return 0xE8EA;  // CellPhone
    case DeviceKind::Microphone:
        return 0xE720;  // Microphone
    default:
        return bluetooth ? 0xE702 : 0xE772;  // Bluetooth / Devices
    }
}

}  // namespace ui
