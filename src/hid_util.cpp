// Portable helpers declared in hid.h: number formatting, level mapping and device-type guesses.
// Shared by the Windows and Linux backends and linked by the driver tests that use tests/fake_hid.h.
#include "hid.h"

#include <cwchar>
#include <cwctype>

std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int MapTo100(int v, int lo, int hi) {
    if (hi <= lo) return Clamp100(v);
    return Clamp100((v - lo) * 100 / (hi - lo));
}

int Interpolate(int mv, const int* mvTable, const int* pctTable, int n) {
    if (mv >= mvTable[0]) return pctTable[0];
    for (int i = 1; i < n; i++) {
        if (mv >= mvTable[i]) {
            int span = mvTable[i - 1] - mvTable[i];
            return pctTable[i] + (pctTable[i - 1] - pctTable[i]) * (mv - mvTable[i]) / (span ? span : 1);
        }
    }
    return pctTable[n - 1];
}

namespace {

// Lower-case words of a product name. '-' stays inside a word ("joy-con", "wh-1000xm5").
std::vector<std::wstring> Words(const std::wstring& name) {
    std::vector<std::wstring> words;
    std::wstring w;
    for (wchar_t ch : name) {
        if (iswalnum((wint_t)ch) || ch == L'-') {
            w.push_back((wchar_t)towlower((wint_t)ch));
        } else if (!w.empty()) {
            words.push_back(w);
            w.clear();
        }
    }
    if (!w.empty()) words.push_back(w);
    return words;
}

}  // namespace

// Matching is per word: a part of a word only counts where no ordinary English word contains it
// ("headset", "earbuds", "airpods"). "ear" and "keys" must start or be a whole word, so "Gear",
// "clear" and "hotkeys" do not turn a device into a headset or a keyboard.
DeviceKind GuessKind(const std::wstring& name) {
    const auto words = Words(name);
    auto any = [&](std::initializer_list<const wchar_t*> parts, bool prefixOnly) {
        for (const auto& w : words)
            for (const wchar_t* p : parts) {
                size_t at = w.find(p);
                if (at != std::wstring::npos && (!prefixOnly || at == 0)) return true;
            }
        return false;
    };
    auto word = [&](const wchar_t* exact) {
        for (const auto& w : words)
            if (w == exact) return true;
        return false;
    };
    if (any({ L"mouse", L"mice", L"trackball" }, false)) return DeviceKind::Mouse;
    if (any({ L"keyboard" }, false) || word(L"keys")) return DeviceKind::Keyboard;
    if (any({ L"controller", L"gamepad", L"xbox", L"dualsense", L"dualshock" }, false) || any({ L"joy-con" }, true))
        return DeviceKind::Gamepad;
    if (any({ L"head", L"buds", L"pods", L"audio", L"speaker", L"arctis", L"virtuoso" }, false) ||
        any({ L"ear", L"wh-", L"wf-", L"cloud", L"void" }, true))
        return DeviceKind::Headset;
    return DeviceKind::Other;
}

DeviceKind KindFromCollections(const std::vector<HidColl>& all, USHORT vid, USHORT pid) {
    bool mouse = false, pad = false, kbd = false;
    for (const auto& c : all) {
        // Linux fills only the top-level usage it could parse; a zero page means "unknown".
        if (c.vid != vid || c.pid != pid || (c.page() && c.page() != 0x01) || !c.usage()) continue;
        mouse |= c.usage() == 0x02;
        pad |= c.usage() == 0x04 || c.usage() == 0x05;
        kbd |= c.usage() == 0x06;
    }
    if (mouse) return DeviceKind::Mouse;
    if (pad) return DeviceKind::Gamepad;
    if (kbd) return DeviceKind::Keyboard;
    return DeviceKind::Other;
}
