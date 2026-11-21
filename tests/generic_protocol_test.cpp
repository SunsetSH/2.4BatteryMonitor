// Pure parts of the generic fallbacks: HID battery values, Bluetooth ids, device types, Fast Pair.
#include "../src/drivers/generic_protocol.h"

#include <cassert>
#include <cstdio>

using namespace generic;

static_assert(KindFromClassOfDevice(0x0200) == DeviceKind::Phone);
static_assert(KindFromClassOfDevice(0x0204) == DeviceKind::Phone);
static_assert(KindFromClassOfDevice(0x0400) == DeviceKind::Headset);
static_assert(KindFromClassOfDevice(0x0540) == DeviceKind::Keyboard);
static_assert(KindFromClassOfDevice(0x0580) == DeviceKind::Mouse);
static_assert(KindFromClassOfDevice(0x0504) == DeviceKind::Gamepad);
static_assert(KindFromClassOfDevice(0x0000) == DeviceKind::Other);

int main() {
    assert(IsBatteryUsage(0x06, 0x20) && IsBatteryUsage(0x85, 0x65));
    assert(!IsBatteryUsage(0x06, 0x21) && !IsBatteryUsage(0x84, 0x65));

    // Logical range 0..255, 0..100, offset ranges; out-of-range values are clamped.
    assert(BatteryPercent(0x046D, 255, 0, 255) == 100);
    assert(BatteryPercent(0x046D, 128, 0, 255) == 50);
    assert(BatteryPercent(0x046D, 0, 0, 255) == 0);
    assert(BatteryPercent(0x046D, 30, 0, 100) == 30);
    assert(BatteryPercent(0x046D, 15, 10, 20) == 50);
    assert(BatteryPercent(0x046D, 300, 0, 255) == 100);
    assert(BatteryPercent(0x046D, 5, 10, 20) == 0);
    // No usable range: only a plain 0..100 value counts.
    assert(BatteryPercent(0x046D, 77, 0, 0) == 77);
    assert(BatteryPercent(0x046D, 177, 0, 0) == -1);
    // Apple reports a plain percentage despite 0..255.
    assert(BatteryPercent(0x05AC, 64, 0, 255) == 64);
    assert(BatteryPercent(0x05AC, 200, 0, 255) == 78);

    assert(BtAddress(L"BTHENUM\\DEV_6CFBED129847\\7&1&0") == 0x6CFBED129847ULL);
    assert(BtAddress(L"BTHLE\\DEV_a0b1c2d3e4f5\\x") == 0xA0B1C2D3E4F5ULL);
    assert(BtAddress(L"BTHENUM\\DEV_6CFBED12984\\x") == 0);  // too short
    assert(BtAddress(L"USB\\VID_046D") == 0 && BtAddress(nullptr) == 0);

    assert(KindFromCategory(L"Communication.Phone") == DeviceKind::Phone);
    assert(KindFromCategory(L"Communication.Phone.Cell") == DeviceKind::Phone);
    assert(KindFromCategory(L"Input.Keyboard") == DeviceKind::Keyboard);
    assert(KindFromCategory(L"Input.Mouse") == DeviceKind::Mouse);
    assert(KindFromCategory(L"Input.Gaming") == DeviceKind::Gamepad);
    assert(KindFromCategory(L"Audio.Headphones") == DeviceKind::Headset);
    assert(KindFromCategory(L"Communication.Headset.Bluetooth") == DeviceKind::Headset);
    assert(KindFromCategory(L"Audio.Speaker") == DeviceKind::Other);

    assert(StripServiceSuffix(L"WH-1000XM5 Hands-Free AG") == L"WH-1000XM5");
    assert(StripServiceSuffix(L"Buds Stereo") == L"Buds");
    assert(StripServiceSuffix(L"Stereo") == L"Stereo");

    // Fast Pair: the lower bud in use; charging only when every reporting bud charges.
    DeviceStatus s;
    ApplyFastPair(s, 80, false, 60, false, 40);
    assert(s.percent == 60 && !s.charging && s.left == 80 && s.right == 60 && s.casePct == 40);
    s = DeviceStatus{};
    ApplyFastPair(s, 90, true, 50, false, -1);
    assert(s.percent == 50 && !s.charging);
    s = DeviceStatus{};
    ApplyFastPair(s, 90, true, 70, true, 100);
    assert(s.percent == 70 && s.charging);
    s = DeviceStatus{};
    ApplyFastPair(s, -1, false, -1, false, 55);
    assert(s.percent == -1 && s.casePct == 55);

    std::printf("generic_protocol_test passed\n");
    return 0;
}
