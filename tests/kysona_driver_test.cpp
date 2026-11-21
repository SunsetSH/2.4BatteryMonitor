// Kysona-protocol driver (Kysona, ATK / VXE, Hitscan, Redragon, HECATE mice) on simulated 17-byte
// reports (tests/fake_hid.h): [08][cmd][0 x14][checksum = 0x55 - sum(0..15)].
#include "fake_hid.h"

#include <cassert>
#include <cstdio>

// The G-Wolves model table lives in glorious.cpp, which this test does not link.
const wchar_t* GWolvesModelName(USHORT) { return L"G-Wolves mouse"; }
bool GWolvesIsCable(USHORT) { return false; }

namespace {

BYTE Sum(const std::vector<BYTE>& r) {
    unsigned s = 0;
    for (int i = 0; i < 16; i++) s += r[i];
    return (BYTE)(0x55 - s);
}

struct Mouse {
    bool online = true;
    bool answersBattery = true;
    BYTE percent = 77, charging = 0;
    bool badChecksum = false;
};

fake::Device& Add(const std::wstring& path, USHORT vid, USHORT pid, const std::wstring& product, Mouse m,
                  USHORT page = 0xFF02, USHORT usage = 0x0002) {
    fake::Device& d = fake::Add(fake::Coll(path, vid, pid, page, usage, 17, 17, 0), product);
    d.onWrite = [m](const std::vector<BYTE>& q, fake::Device& self) {
        if (q[0] != 0x08 || q[16] != Sum(q)) return;
        if (q[1] == 0x04 && !m.answersBattery) return;
        std::vector<BYTE> r(17, 0);
        r[0] = 0x08;
        r[1] = q[1];
        if (q[1] == 0x03) r[6] = m.online;
        if (q[1] == 0x04) {
            r[6] = m.percent;
            r[7] = m.charging;
            r[8] = 0x0F;
            r[9] = 0x6E;  // 3950 mV
        }
        r[16] = (BYTE)(Sum(r) ^ (m.badChecksum ? 0x01 : 0));
        self.input.push_back(r);
    };
    return d;
}

PollContext Poll() {
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    PollKysona(ctx);
    return ctx;
}

}  // namespace

int main() {
    // Online mouse on its dongle: level, millivolts, name without " Dongle".
    fake::Reset();
    Add(L"rx", 0x3554, 0xF50D, L"Kysona M600 Dongle", Mouse{});
    PollContext ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 77 && !ctx.out[0].charging && ctx.out[0].millivolts == 3950);
    assert(ctx.out[0].name == L"Kysona M600" && ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].key == L"kysona:3554:F50D" && ctx.out[0].via == L"2.4 GHz receiver" &&
           ctx.out[0].graceSec == 1800);
    assert(ctx.IsClaimed(0x3554, 0xF50D));
    for (const auto& q : fake::devices[L"rx"].writes) assert(q[0] == 0x08 && (q[1] == 0x03 || q[1] == 0x04));

    // 0% and 100%, charging.
    for (BYTE pct : { 0, 100 }) {
        fake::Reset();
        Mouse m;
        m.percent = pct;
        m.charging = 1;
        Add(L"rx", 0x3554, 0xF50D, L"Kysona M600 Dongle", m);
        ctx = Poll();
        assert(ctx.out.size() == 1 && ctx.out[0].percent == pct && ctx.out[0].charging);
    }
    // An impossible level is not shown.
    fake::Reset();
    {
        Mouse m;
        m.percent = 0xFF;
        Add(L"rx", 0x3554, 0xF50D, L"Kysona M600 Dongle", m);
    }
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == -1);

    // Mouse switched off: the receiver is claimed, nothing listed.
    fake::Reset();
    {
        Mouse m;
        m.online = false;
        Add(L"rx", 0x3554, 0xF50D, L"Kysona M600 Dongle", m);
    }
    ctx = Poll();
    assert(ctx.out.empty() && ctx.IsClaimed(0x3554, 0xF50D));

    // Receiver says online but the mouse does not answer: asleep, last value kept by the app.
    fake::Reset();
    {
        Mouse m;
        m.answersBattery = false;
        Add(L"rx", 0x3554, 0xF50D, L"Kysona M600 Dongle", m);
    }
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].percent == -1);

    // Wrong checksums: no answer, no claim.
    fake::Reset();
    {
        Mouse m;
        m.badChecksum = true;
        Add(L"rx", 0x3554, 0xF50D, L"Kysona M600 Dongle", m);
    }
    ctx = Poll();
    assert(ctx.out.empty() && !ctx.IsClaimed(0x3554, 0xF50D));

    // VXE R1 on cable and receiver: one entry under the receiver's id, charging from the cable.
    fake::Reset();
    {
        Mouse cable;
        cable.charging = 1;
        Add(L"rx", 0x3554, 0xF58A, L"VXE NordicMouse 1K Dongle", Mouse{});
        Add(L"cable", 0x3554, 0xF58C, L"VXE R1", cable);
    }
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"VXE Dragonfly R1" && ctx.out[0].charging);
    assert(ctx.out[0].key == L"kysona:3554:F58A");

    // Hitscan names itself only as a receiver.
    fake::Reset();
    Add(L"hs", 0x3770, 0x0200, L"Hitscan USB Receiver", Mouse{});
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"Hitscan Hyperlight");

    // Receivers of other protocols on the same vendor ids never get mouse frames.
    fake::Reset();
    Add(L"aula", 0x3554, 0xFA09, L"BY receiver", Mouse{});
    Add(L"atk8k", 0x373B, 0x1178, L"ATK 8K Nano", Mouse{});
    Add(L"hecate", 0x35BB, 0xA009, L"G2 Wireless 7.1", Mouse{});
    ctx = Poll();
    assert(ctx.out.empty());
    for (const auto* p : { L"aula", L"atk8k", L"hecate" }) assert(fake::devices[p].writes.empty());

    // A collection without 17-byte reports and other vendors are not opened.
    fake::Reset();
    fake::Add(fake::Coll(L"kbd", 0x3554, 0xF50D, 0x0001, 0x0006, 9, 1, 0));
    fake::Add(fake::Coll(L"other", 0x1532, 0xF50D, 0xFF02, 0x0002, 17, 17, 0));
    ctx = Poll();
    assert(ctx.out.empty() && fake::devices[L"kbd"].opens == 0 && fake::devices[L"other"].opens == 0);

    std::printf("Kysona driver tests passed (level, charging, bounds, offline, asleep, checksum, cable + dongle, "
                "Hitscan, other protocols, filtering)\n");
    return 0;
}
