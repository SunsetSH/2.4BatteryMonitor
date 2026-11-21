// Display rules shared by the tray, the widget, the overview and --list.
#include "../src/status_text.h"

#include <cassert>
#include <cstdio>

using namespace status_text;

namespace {
std::wstring En(const wchar_t* s) { return s; }

DeviceStatus Device(int pct) {
    DeviceStatus s;
    s.percent = pct;
    return s;
}
}  // namespace

int main() {
    // Charging and full never both: full wins, also when a charging device reports 100%.
    DeviceStatus s = Device(100);
    s.charging = true;
    Shown v = ShownState(s, -1);
    assert(v.full && !v.charging && v.percent == 100);
    s.percent = 60;
    v = ShownState(s, -1);
    assert(v.charging && !v.full);
    s.full = true;
    assert(ShownState(s, -1).full && !ShownState(s, -1).charging);

    // Asleep or cached: dimmed, never charging, last known level shown.
    s = Device(-1);
    s.asleep = true;
    s.charging = true;
    v = ShownState(s, 42);
    assert(v.dim && !v.charging && !v.full && v.percent == 42);

    // Offline: no stale level at all.
    s = Device(-1);
    s.connected = false;
    v = ShownState(s, 80);
    assert(v.offline && v.percent == -1);
    assert(Describe(s, 80, En) == L"off / not connected");

    // Levels follow the device threshold; charging is never low.
    assert(LevelOf(ShownState(Device(15), -1), 15) == Level::Low);
    assert(LevelOf(ShownState(Device(16), -1), 15) == Level::Warn);
    assert(LevelOf(ShownState(Device(30), -1), 15) == Level::Warn);
    assert(LevelOf(ShownState(Device(31), -1), 15) == Level::Normal);
    assert(LevelOf(ShownState(Device(25), -1), 30) == Level::Low);
    assert(LevelOf(ShownState(Device(0), -1), 5) == Level::Low);
    assert(LevelOf(ShownState(Device(-1), -1), 15) == Level::Normal);
    s = Device(5);
    s.charging = true;
    assert(LevelOf(ShownState(s, -1), 15) == Level::Normal);

    // Words.
    assert(Describe(Device(85), -1, En) == L"85%");
    s = Device(100);
    s.charging = true;
    assert(Describe(s, -1, En) == L"100%, fully charged");
    s = Device(40);
    s.charging = true;
    assert(Describe(s, -1, En) == L"40%, charging");
    s = Device(-1);
    s.asleep = true;
    assert(Describe(s, 33, En) == L"33% (last known), asleep");
    assert(Describe(Device(-1), -1, En) == L"unknown");
    s = Device(70);
    s.cached = true;
    s.charging = true;
    assert(Describe(s, -1, En) == L"70% (last known)");
    s = Device(80);
    s.levelText = L"Good";
    assert(Describe(s, -1, En) == L"Good");

    // Earbuds: structured levels win over the driver note; missing parts are left out.
    s = Device(75);
    s.left = 80;
    s.right = 75;
    s.casePct = 40;
    s.detail = L"ignored";
    assert(Describe(s, -1, En) == L"75% (L 80%, R 75%, case 40%)");
    s.casePct = -1;
    s.left = -1;
    assert(BudsText(s, En) == L"R 75%");
    s = Device(60);
    s.detail = L"docked";
    assert(Describe(s, -1, En) == L"60% (docked)");

    // Translation and placeholders.
    auto ru = [](const wchar_t* en) -> std::wstring {
        std::wstring e = en;
        if (e == L"case {percent}%") return L"кейс {percent}%";
        if (e == L"charging") return L"заряжается";
        return e;
    };
    s = Device(50);
    s.casePct = 20;
    s.charging = true;
    assert(Describe(s, -1, ru) == L"50% (кейс 20%), заряжается");
    assert(Fill(L"{a} and {a}", { { L"a", L"{a}x" } }) == L"{a}x and {a}x");

    std::printf("status_text_test passed\n");
    return 0;
}
