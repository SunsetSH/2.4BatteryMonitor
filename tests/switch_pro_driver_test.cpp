// Switch Pro Controller and Joy-Con on simulated full input reports 0x30 (tests/fake_hid.h):
// byte 2 bits 7..5 = level 0..4, bit 4 = charging (SDL SDL_hidapi_switch.c).
#include "fake_hid.h"

#include <cassert>
#include <cstdio>

namespace {

std::vector<BYTE> Full(BYTE battery, BYTE id = 0x30, size_t size = 49) {
    std::vector<BYTE> r(size, 0);
    r[0] = id;
    if (size > 2) r[2] = battery;
    return r;
}

fake::Device& Pad(const std::wstring& path, USHORT pid, const std::wstring& product = L"") {
    return fake::Add(fake::Coll(path, 0x057E, pid, 0x01, 0x05, 49, 0, 0), product);
}

PollContext Poll() {
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    PollSwitchPro(ctx);
    return ctx;
}

}  // namespace

int main() {
    // Pro Controller: level 4 (full), not charging.
    fake::Reset();
    Pad(L"pro", 0x2009, L"Pro Controller").input = { Full(0x80) };
    PollContext ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].levelText == L"Full" && !ctx.out[0].charging);
    assert(ctx.out[0].name == L"Pro Controller" && ctx.out[0].kind == DeviceKind::Gamepad);
    assert(ctx.out[0].key.rfind(L"switchpro:", 0) == 0 && ctx.IsClaimed(0x057E, 0x2009));

    // Joy-Con (L) and (R): their own names and keys, levels and charging from the same byte.
    fake::Reset();
    Pad(L"left", 0x2006).input = { Full(0x50) };   // level 2, charging
    Pad(L"right", 0x2007).input = { Full(0x20) };  // level 1, not charging
    ctx = Poll();
    assert(ctx.out.size() == 2);
    const DeviceStatus& l = ctx.out[0].name == L"Joy-Con (L)" ? ctx.out[0] : ctx.out[1];
    const DeviceStatus& r = ctx.out[0].name == L"Joy-Con (L)" ? ctx.out[1] : ctx.out[0];
    assert(l.name == L"Joy-Con (L)" && l.percent == 50 && l.levelText == L"Low" && l.charging);
    assert(r.name == L"Joy-Con (R)" && r.percent == 25 && r.levelText == L"Critical" && !r.charging);
    assert(l.key.rfind(L"joycon:", 0) == 0 && l.key != r.key);
    assert(ctx.IsClaimed(0x057E, 0x2006) && ctx.IsClaimed(0x057E, 0x2007));

    // Empty and charging at full: level 0, and no charging flag once full.
    fake::Reset();
    Pad(L"pro", 0x2009).input = { Full(0x00) };
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0 && ctx.out[0].levelText == L"Empty");
    fake::Reset();
    Pad(L"pro", 0x2009).input = { Full(0x90) };
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && !ctx.out[0].charging);

    // Simple reports (0x3F), short reports and impossible levels are skipped; silence is no device.
    fake::Reset();
    Pad(L"pro", 0x2009).input = { Full(0x80, 0x3F), Full(0x80, 0x30, 8), Full(0xA0), Full(0x60) };
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 75);
    fake::Reset();
    Pad(L"pro", 0x2009);
    ctx = Poll();
    assert(ctx.out.empty() && !ctx.IsClaimed(0x057E, 0x2009));

    // Other Nintendo ids and other collections are not opened; nothing is ever written.
    fake::Reset();
    Pad(L"grip", 0x200E).input = { Full(0x80) };
    fake::Add(fake::Coll(L"vendor", 0x057E, 0x2009, 0xFF00, 0x0001, 64, 64, 0));
    ctx = Poll();
    assert(ctx.out.empty() && fake::devices[L"grip"].opens == 0 && fake::devices[L"vendor"].opens == 0);
    for (const auto& [path, d] : fake::devices) assert(d.writes.empty() && d.featuresSet.empty());

    std::printf("Switch driver tests passed (Pro Controller, Joy-Con L / R, levels, charging, skipped reports, "
                "silence, filtering)\n");
    return 0;
}
