// Portable HID helpers: device type from a product name, level mapping and voltage tables.
#include "../src/hid.h"

#include <cassert>
#include <cstdio>

int main() {
    // Whole words or word starts only: "Gear", "clear" and "hotkeys" are not headsets or keyboards.
    assert(GuessKind(L"Endgame Gear OP1w") == DeviceKind::Other);
    assert(GuessKind(L"Clear Pearl Year Edition") == DeviceKind::Other);
    assert(GuessKind(L"Hotkeys Pad") == DeviceKind::Other);
    assert(GuessKind(L"MX Keys") == DeviceKind::Keyboard);
    assert(GuessKind(L"Wireless Keyboard K380") == DeviceKind::Keyboard);
    assert(GuessKind(L"Nothing Ear (2)") == DeviceKind::Headset);
    assert(GuessKind(L"Galaxy Buds3 Pro") == DeviceKind::Headset);
    assert(GuessKind(L"AirPods Pro") == DeviceKind::Headset);
    assert(GuessKind(L"WH-1000XM5") == DeviceKind::Headset);
    assert(GuessKind(L"Sony WF-1000XM4") == DeviceKind::Headset);
    assert(GuessKind(L"HyperX Cloud III Wireless") == DeviceKind::Headset);
    assert(GuessKind(L"Corsair VOID ELITE") == DeviceKind::Headset);
    assert(GuessKind(L"Arctis Nova 7") == DeviceKind::Headset);
    assert(GuessKind(L"Wireless Headset") == DeviceKind::Headset);
    assert(GuessKind(L"Earbuds X") == DeviceKind::Headset);
    assert(GuessKind(L"Razer Mouse Dock") == DeviceKind::Mouse);
    assert(GuessKind(L"Joy-Con (L)") == DeviceKind::Gamepad);
    assert(GuessKind(L"Xbox Wireless Controller") == DeviceKind::Gamepad);
    assert(GuessKind(L"DualSense Wireless Controller") == DeviceKind::Gamepad);
    assert(GuessKind(L"") == DeviceKind::Other);

    assert(Hex4(0x046D) == L"046D");
    assert(Clamp100(-5) == 0 && Clamp100(140) == 100 && Clamp100(42) == 42);
    assert(MapTo100(3500, 3000, 4000) == 50 && MapTo100(5000, 3000, 4000) == 100 && MapTo100(7, 5, 5) == 7);
    const int mv[] = { 4200, 3800, 3500 }, pct[] = { 100, 50, 0 };
    assert(Interpolate(4300, mv, pct, 3) == 100);
    assert(Interpolate(4000, mv, pct, 3) == 75);
    assert(Interpolate(3400, mv, pct, 3) == 0);

    std::vector<HidColl> colls(2);
    colls[0].vid = colls[1].vid = 0x1234;
    colls[0].pid = colls[1].pid = 0x0001;
    colls[0].caps.UsagePage = 0x01;
    colls[0].caps.Usage = 0x06;
    colls[1].caps.UsagePage = 0x01;
    colls[1].caps.Usage = 0x02;
    assert(KindFromCollections(colls, 0x1234, 0x0001) == DeviceKind::Mouse);
    assert(KindFromCollections(colls, 0x1234, 0x0002) == DeviceKind::Other);
    colls[1].caps.UsagePage = 0xFF00;
    assert(KindFromCollections(colls, 0x1234, 0x0001) == DeviceKind::Keyboard);

    std::printf("hid_util_test passed\n");
    return 0;
}
