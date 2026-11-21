// PlayStation driver on simulated input reports (tests/fake_hid.h). Byte positions as the driver
// documents them from Linux hid-playstation.c.
#include "fake_hid.h"

#include <cassert>
#include <cstdio>

namespace {

std::vector<BYTE> Report(BYTE id, size_t at, BYTE value, size_t size = 78) {
    std::vector<BYTE> r(size, 0);
    r[0] = id;
    if (at < size) r[at] = value;
    return r;
}

fake::Device& Pad(const std::wstring& path, USHORT pid, bool bluetooth) {
    return fake::Add(fake::Coll(path, 0x054C, pid, 0x01, 0x05, bluetooth ? 547 : 64, 0, 64));
}

DeviceStatus PollOne() {
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    PollPlayStation(ctx);
    assert(ctx.out.size() <= 1);
    if (ctx.out.empty()) return DeviceStatus{ L"", L"", L"", DeviceKind::Other, -2 };
    assert(ctx.IsClaimed(0x054C, ctx.out[0].pid ? ctx.out[0].pid : fake::devices.begin()->second.coll.pid));
    return ctx.out[0];
}

}  // namespace

int main() {
    // DualShock 4 over USB: byte 30, bit 4 = cable, level 11 = full.
    fake::Reset();
    Pad(L"ds4", 0x09CC, false).input = { Report(0x01, 30, 0x1B) };
    DeviceStatus s = PollOne();
    assert(s.name == L"DualShock 4" && s.percent == 100 && !s.charging && s.via == L"USB cable");
    assert(s.kind == DeviceKind::Gamepad);

    fake::Reset();
    Pad(L"ds4", 0x09CC, false).input = { Report(0x01, 30, 0x15) };
    s = PollOne();
    assert(s.percent == 50 && s.charging);

    // DualShock 4 over Bluetooth: the calibration feature report is read first, then report 11 byte 32.
    // A reduced report 01 (sent before full reports start) is skipped.
    fake::Reset();
    {
        fake::Device& d = Pad(L"ds4bt", 0x05C4, true);
        d.features[0x05] = { 0x05 };
        d.input = { Report(0x01, 30, 0x0A, 10), Report(0x11, 32, 0x07) };
    }
    s = PollOne();
    assert(s.percent == 75 && !s.charging && s.via == L"Bluetooth");

    // DualSense over USB: byte 53, level 0..10 and state (1 charging, 2 full).
    fake::Reset();
    Pad(L"ds", 0x0CE6, false).input = { Report(0x01, 53, 0x08) };
    s = PollOne();
    assert(s.name == L"DualSense" && s.percent == 85 && !s.charging);
    fake::Reset();
    Pad(L"ds", 0x0CE6, false).input = { Report(0x01, 53, 0x13) };
    s = PollOne();
    assert(s.percent == 35 && s.charging);
    fake::Reset();
    Pad(L"ds", 0x0CE6, false).input = { Report(0x01, 53, 0x2A) };
    s = PollOne();
    assert(s.percent == 100 && s.charging);

    // DualSense Edge over Bluetooth: report 31 byte 54.
    fake::Reset();
    {
        fake::Device& d = Pad(L"edge", 0x0DF2, true);
        d.features[0x05] = { 0x05 };
        d.input = { Report(0x31, 54, 0x00) };
    }
    s = PollOne();
    assert(s.name == L"DualSense Edge" && s.percent == 5 && s.via == L"Bluetooth");

    // The wireless adapter without a controller reports 0: no device.
    fake::Reset();
    Pad(L"adapter", 0x0BA0, false).input = { Report(0x01, 30, 0x00) };
    assert(PollOne().percent == -2);

    // Short or unknown reports and silence: nothing is invented.
    fake::Reset();
    Pad(L"ds", 0x0CE6, false).input = { Report(0x01, 53, 0x08, 40), Report(0x02, 53, 0x08), Report(0x01, 0, 0, 5) };
    assert(PollOne().percent == -2);
    fake::Reset();
    Pad(L"ds", 0x0CE6, false);
    const ULONGLONG start = GetTickCount64();
    assert(PollOne().percent == -2 && GetTickCount64() - start < 1500);

    // Only the gamepad collection of known controllers is opened.
    fake::Reset();
    fake::Add(fake::Coll(L"touchpad", 0x054C, 0x0CE6, 0x0D, 0x05, 64, 0, 0)).input = { Report(0x01, 53, 0x08) };
    fake::Add(fake::Coll(L"other", 0x054C, 0x0268, 0x01, 0x05, 64, 0, 0)).input = { Report(0x01, 53, 0x08) };
    assert(PollOne().percent == -2);
    assert(fake::devices[L"touchpad"].opens == 0 && fake::devices[L"other"].opens == 0);

    // Two controllers are two devices with different keys.
    fake::Reset();
    Pad(L"a", 0x0CE6, false).input = { Report(0x01, 53, 0x05) };
    Pad(L"b", 0x0CE6, true).input = { Report(0x31, 54, 0x09) };
    fake::devices[L"b"].features[0x05] = { 0x05 };
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    PollPlayStation(ctx);
    assert(ctx.out.size() == 2 && ctx.out[0].key != ctx.out[1].key);

    std::printf("PlayStation driver tests passed (DS4 USB / Bluetooth, DualSense, Edge, charging, full, adapter, "
                "malformed, silence, filtering, two pads)\n");
    return 0;
}
