// Corsair driver on simulated replies (tests/fake_hid.h): Void-family headsets (C9 64), Bragi GETs in
// both framings and Compx mice, with the layouts the driver documents.
#include "fake_hid.h"

#include <cassert>
#include <cstdio>

namespace {

PollContext Poll() {
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    PollCorsair(ctx);
    return ctx;
}

// Void: C9 64 -> [2] level (bit 7 = mic up), [4] status.
void Void(BYTE level, BYTE status) {
    fake::Device& d =
        fake::Add(fake::Coll(L"void", 0x1B1C, 0x0A14, 0xFFC5, 0x0001, 5, 2, 0), L"CORSAIR VOID ELITE Wireless");
    d.onWrite = [level, status](const std::vector<BYTE>& q, fake::Device& self) {
        if (q[0] == 0xC9 && q[1] == 0x64) self.input.push_back({ 0x64, 0, level, 0, status });
    };
}

// Bragi properties by target: 0F battery (0..1000), 10 charge state, 12 paired product id.
struct Target {
    BYTE id;
    unsigned battery, charge, product;
};

// A Bragi device answering prefixed GETs (02 <target> 02 <prop>) or only plain ones (00 <target> 02 <prop>).
void Bragi(const std::wstring& path, USHORT pid, const std::wstring& product, std::vector<Target> targets, bool plain) {
    fake::Device& d = fake::Add(fake::Coll(path, 0x1B1C, pid, 0xFF42, 0x0001, 64, 64, 0), product);
    d.onWrite = [targets, plain](const std::vector<BYTE>& q, fake::Device& self) {
        const bool isPlain = q[0] == 0x00 && q[1] >= 0x08 && q[2] == 0x02;
        const bool isPrefixed = q[0] == 0x02 && q[2] == 0x02;
        if (plain ? !isPlain : !isPrefixed) return;  // this firmware ignores the other framing
        const BYTE target = q[1], prop = q[3];
        BYTE status = 5;
        unsigned value = 0;
        for (const auto& t : targets)
            if (t.id == target) {
                status = 0;
                value = prop == 0x0F ? t.battery : prop == 0x10 ? t.charge : prop == 0x12 ? t.product : 0;
                if (prop != 0x0F && prop != 0x10 && prop != 0x12) status = 5;
            }
        std::vector<BYTE> r(64, 0);
        const size_t o = plain ? 0 : 1;
        if (!plain) r[0] = 0x02;
        r[o] = target == 0x08 ? 0x00 : 0x01;
        r[o + 1] = 0x02;
        r[o + 2] = status;
        for (int i = 0; i < 4; i++) r[o + 3 + i] = (BYTE)(value >> (8 * i));
        self.input.push_back({ 0x03, 0x01, 0x02 });  // an unrelated notification first
        self.input.push_back(r);
    };
}

BYTE CompxSum(const std::vector<BYTE>& r) {
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) sum += r[i];
    return (BYTE)(0x55 - sum);
}

void Compx(bool online, BYTE percent, BYTE charging, bool badChecksum = false) {
    fake::Device& d = fake::Add(fake::Coll(L"sabre", 0x1B1C, 0x2B2A, 0xFF02, 0x0002, 17, 17, 0));
    d.onWrite = [=](const std::vector<BYTE>& q, fake::Device& self) {
        if (q[0] != 0x08) return;
        std::vector<BYTE> r(17, 0);
        r[0] = 0x08;
        r[1] = q[1];
        if (q[1] == 0x03) r[6] = online ? 1 : 0;
        if (q[1] == 0x04) {
            r[6] = percent;
            r[7] = charging;
            r[8] = 0x0F;
            r[9] = 0xA0;
        }
        r[16] = (BYTE)(CompxSum(r) ^ (badChecksum ? 0xFF : 0));
        self.input.push_back(r);
    };
}

}  // namespace

int main() {
    // Void: level with the mic bit masked, status 1 ok, 4 charging, 0 offline.
    fake::Reset();
    Void(0x80 | 72, 1);
    PollContext ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 72 && !ctx.out[0].charging && ctx.out[0].connected);
    assert(ctx.out[0].kind == DeviceKind::Headset && ctx.IsClaimed(0x1B1C, 0x0A14));
    fake::Reset();
    Void(40, 4);
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 40 && ctx.out[0].charging);
    fake::Reset();
    Void(40, 0);
    ctx = Poll();
    assert(ctx.out.size() == 1 && !ctx.out[0].connected && ctx.out[0].percent == -1);
    fake::Reset();
    fake::Add(fake::Coll(L"void", 0x1B1C, 0x0A14, 0xFFC5, 0x0001, 5, 2, 0));  // silent: no device
    ctx = Poll();
    assert(ctx.out.empty() && !ctx.IsClaimed(0x1B1C, 0x0A14));

    // Bragi receiver with a mouse paired on target 09: 850 -> 85%, product id names the mouse.
    fake::Reset();
    Bragi(L"rx", 0x2B00, L"CORSAIR SLIPSTREAM WIRELESS USB Receiver", { { 0x09, 850, 1, 0x1B7E } }, false);
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 85 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"Corsair Dark Core RGB Pro SE" && ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].via == L"Corsair receiver" && ctx.out[0].key == L"corsair:2B00:9");
    // Only GETs (02) were sent.
    for (const auto& q : fake::devices[L"rx"].writes) assert(q[2] == 0x02);

    // A wired headset answers on target 08; 0Axx product ids are headsets; 3 = fully charged.
    fake::Reset();
    Bragi(L"hs", 0x0A64, L"CORSAIR HS80 MAX", { { 0x08, 1000, 3, 0 } }, false);
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].charging);
    assert(ctx.out[0].kind == DeviceKind::Headset && ctx.out[0].via == L"USB / Bluetooth");

    // Newer receivers answer only the plain framing.
    fake::Reset();
    Bragi(L"rx", 0x2B00, L"CORSAIR SLIPSTREAM WIRELESS USB Receiver", { { 0x09, 333, 0, 0x0A55 } }, true);
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 33 && !ctx.out[0].charging);
    assert(ctx.out[0].kind == DeviceKind::Headset && ctx.out[0].name == L"Corsair wireless headset");

    // A level above 1000 is not a level.
    fake::Reset();
    Bragi(L"rx", 0x2B00, L"CORSAIR SLIPSTREAM WIRELESS USB Receiver", { { 0x09, 5000, 0, 0 } }, false);
    ctx = Poll();
    assert(ctx.out.empty() && !ctx.IsClaimed(0x1B1C, 0x2B00));

    // Compx mouse: online, 67%, charging, millivolts.
    fake::Reset();
    Compx(true, 67, 1);
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 67 && ctx.out[0].charging && ctx.out[0].millivolts == 4000);
    assert(ctx.out[0].name == L"Corsair Sabre V2 Pro Ultralight" && ctx.out[0].graceSec == 1800);
    // Offline: claimed, not listed.
    fake::Reset();
    Compx(false, 0, 0);
    ctx = Poll();
    assert(ctx.out.empty() && ctx.IsClaimed(0x1B1C, 0x2B2A));
    // Wrong checksums are ignored: no answer, no device, no claim.
    fake::Reset();
    Compx(true, 67, 0, true);
    ctx = Poll();
    assert(ctx.out.empty() && !ctx.IsClaimed(0x1B1C, 0x2B2A));

    // Other vendors are not opened.
    fake::Reset();
    fake::Add(fake::Coll(L"other", 0x1532, 0x0A14, 0xFFC5, 0x0001, 5, 2, 0));
    ctx = Poll();
    assert(ctx.out.empty() && fake::devices[L"other"].opens == 0);

    std::printf("Corsair driver tests passed (Void levels / charging / offline / silent, Bragi receiver + wired + "
                "plain framing, bounds, Compx online / offline / checksum, filtering)\n");
    return 0;
}
