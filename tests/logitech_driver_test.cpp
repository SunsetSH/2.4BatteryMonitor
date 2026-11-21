// Logitech HID++ driver on a simulated receiver and cable (tests/fake_hid.h). The replies follow the
// HID++ 2.0 framing the driver documents: [report][index][feature index][fn << 4 | sw id][params].
#include "fake_hid.h"

#include <cassert>
#include <cstdio>

namespace {

constexpr BYTE kSw = 0x0D;  // the driver's software id

struct Mouse {
    BYTE index = 1;          // device index on the receiver (0xFF: direct link)
    int protocol = 4;        // HID++ major version; 1 = HID++ 1.0 (answers the ping with an error)
    std::string name = "G502 X";
    BYTE type = 3;           // mouse
    bool unifiedBattery = true;
    BYTE soc = 85, level = 0, status = 0;  // 0x1004 get_status
    int millivolts = 0;      // > 0: only 0x1001 BATTERY_VOLTAGE
    BYTE unit[4] = { 0x12, 0x34, 0x56, 0x78 };
    BYTE reg0d = 0;          // HID++ 1.0 register 0x0D battery %
};

// Feature table of the simulated device: feature id -> index.
BYTE IndexOf(const Mouse& m, USHORT feature) {
    switch (feature) {
    case 0x0003:
        return 7;
    case 0x0005:
        return 5;
    case 0x1004:
        return m.unifiedBattery && !m.millivolts ? 6 : 0;
    case 0x1001:
        return m.millivolts ? 8 : 0;
    default:
        return 0;
    }
}

std::vector<BYTE> Long(BYTE idx, BYTE b2, BYTE b3, std::initializer_list<BYTE> params) {
    std::vector<BYTE> r(20, 0);
    r[0] = 0x11;
    r[1] = idx;
    r[2] = b2;
    r[3] = b3;
    size_t i = 4;
    for (BYTE v : params) r[i++] = v;
    return r;
}

// Answers the driver's requests for the devices paired with one receiver (or one direct link).
void Responder(const std::vector<Mouse>& mice, const std::vector<BYTE>& q, fake::Device& self) {
    const BYTE idx = q[1], b2 = q[2], b3 = q[3];
    const Mouse* m = nullptr;
    for (const auto& x : mice)
        if (x.index == idx) m = &x;
    if (!m) {  // nothing paired at this index: HID++ 1.0 error "unknown device"
        self.input.push_back({ 0x10, idx, 0x8F, b2, b3, 0x09, 0 });
        return;
    }
    if (m->protocol == 1) {
        if (b2 == 0x00)
            self.input.push_back({ 0x10, idx, 0x8F, b2, b3, 0x01, 0 });  // invalid sub id
        else if (b2 == 0x81 && b3 == 0x0D)
            self.input.push_back({ 0x10, idx, 0x81, 0x0D, m->reg0d, 0, 0x22 });
        else
            self.input.push_back({ 0x10, idx, 0x8F, b2, b3, 0x02, 0 });
        return;
    }
    const BYTE fn = b3 >> 4;
    if (b2 == 0x00 && fn == 1) {
        self.input.push_back(Long(idx, 0, b3, { (BYTE)m->protocol, 2, 0x5A }));
        return;
    }
    if (b2 == 0x00 && fn == 0) {
        self.input.push_back(Long(idx, 0, b3, { IndexOf(*m, (USHORT)(q[4] << 8 | q[5])) }));
        return;
    }
    if (b2 == 5 && fn == 0) {
        self.input.push_back(Long(idx, 5, b3, { (BYTE)m->name.size() }));
        return;
    }
    if (b2 == 5 && fn == 1) {
        std::vector<BYTE> r = Long(idx, 5, b3, {});
        for (size_t i = q[4], o = 4; i < m->name.size() && o < 20; i++, o++) r[o] = (BYTE)m->name[i];
        self.input.push_back(r);
        return;
    }
    if (b2 == 5 && fn == 2) {
        self.input.push_back(Long(idx, 5, b3, { m->type }));
        return;
    }
    if (b2 == 6 && fn == 1) {
        self.input.push_back(Long(idx, 6, b3, { m->soc, m->level, m->status }));
        return;
    }
    if (b2 == 8 && fn == 0) {
        self.input.push_back(Long(idx, 8, b3, { (BYTE)(m->millivolts >> 8), (BYTE)m->millivolts, 0 }));
        return;
    }
    if (b2 == 7 && fn == 0) {
        self.input.push_back(Long(idx, 7, b3, { 2, m->unit[0], m->unit[1], m->unit[2], m->unit[3] }));
        return;
    }
    self.input.push_back({ 0x11, idx, 0xFF, b2, b3, 0x02 });  // HID++ 2.0 error: invalid argument
}

// A receiver's short and long HID++ collections; replies go to the long one.
void Receiver(const std::wstring& id, USHORT pid, const std::wstring& product, std::vector<Mouse> mice) {
    const std::wstring base = L"\\\\?\\hid#vid_046d&pid_" + id + L"&mi_02&col0";
    fake::Device& s = fake::Add(fake::Coll(base + L"1#8&1&0&0000#{x}", 0x046D, pid, 0xFF00, 0x0001, 7, 7, 0), product);
    fake::Device& l =
        fake::Add(fake::Coll(base + L"2#8&1&0&0001#{x}", 0x046D, pid, 0xFF00, 0x0002, 20, 20, 0), product);
    const std::wstring longPath = l.coll.path;
    auto reply = [mice, longPath](const std::vector<BYTE>& q, fake::Device&) {
        Responder(mice, q, fake::devices[longPath]);
    };
    s.onWrite = reply;
    l.onWrite = reply;
}

PollContext Context() {
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    return ctx;
}

}  // namespace

int main() {
    // Receiver with a mouse on index 1 (unified battery, 85%, discharging).
    fake::Reset();
    Receiver(L"c539", 0xC539, L"USB Receiver", { Mouse{} });
    PollContext ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].name == L"Logitech G502 X" && ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].percent == 85 && !ctx.out[0].charging && !ctx.out[0].full);
    assert(ctx.out[0].key == L"logi:unit:12345678");  // the unit id, not the receiver path
    assert(ctx.out[0].via == L"Logitech receiver" && ctx.out[0].graceSec == 1800);
    assert(ctx.IsClaimed(0x046D, 0xC539));

    // Charging, then charging complete.
    for (BYTE status : { 1, 3 }) {
        fake::Reset();
        Mouse m;
        m.soc = status == 3 ? 100 : 42;
        m.status = status;
        Receiver(L"c539", 0xC539, L"USB Receiver", { m });
        ctx = Context();
        PollLogitech(ctx);
        assert(ctx.out.size() == 1 && ctx.out[0].charging);
        assert(ctx.out[0].full == (status == 3));
    }

    // Coarse level only (state of charge 0): level bits map to a percentage and a word.
    fake::Reset();
    {
        Mouse m;
        m.soc = 0;
        m.level = 2;
        Receiver(L"c539", 0xC539, L"USB Receiver", { m });
    }
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 20 && ctx.out[0].levelText == L"Low");

    // Battery voltage only: millivolts and an interpolated level.
    fake::Reset();
    {
        Mouse m;
        m.millivolts = 3989;
        m.name = "MX Keys";
        m.type = 0;
        Receiver(L"c52b", 0xC52B, L"USB Receiver", { m });
    }
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].millivolts == 3989 && ctx.out[0].percent == 80);
    assert(ctx.out[0].name == L"MX Keys" && ctx.out[0].kind == DeviceKind::Keyboard);

    // HID++ 1.0 device: register 0x0D.
    fake::Reset();
    {
        Mouse m;
        m.protocol = 1;
        m.reg0d = 64;
        Receiver(L"c52b", 0xC52B, L"USB Receiver", { m });
    }
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 64 && ctx.out[0].charging);

    // Two mice on one receiver are two devices.
    fake::Reset();
    {
        Mouse a, b;
        b.index = 2;
        b.soc = 33;
        b.unit[3] = 0x79;
        Receiver(L"c548", 0xC548, L"USB Receiver", { a, b });
    }
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.size() == 2 && ctx.out[0].percent == 85 && ctx.out[1].percent == 33);
    assert(ctx.out[0].key != ctx.out[1].key);

    // A receiver with nothing paired reports nothing and claims nothing.
    fake::Reset();
    Receiver(L"c539", 0xC539, L"USB Receiver", {});
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.empty() && !ctx.IsClaimed(0x046D, 0xC539));

    // Another vendor's collection is not touched.
    fake::Reset();
    fake::Add(fake::Coll(L"other", 0x1532, 0x00B7, 0xFF00, 0x0002, 20, 20, 0));
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.empty() && fake::devices[L"other"].opens == 0);

    // A silent receiver: every index times out, the poll still ends (no reply is never a device).
    fake::Reset();
    {
        const std::wstring base = L"\\\\?\\hid#vid_046d&pid_c539&mi_02&col0";
        fake::Add(fake::Coll(base + L"2#8&1&0&0001#{x}", 0x046D, 0xC539, 0xFF00, 0x0002, 20, 20, 0), L"USB Receiver");
    }
    ctx = Context();
    const ULONGLONG start = GetTickCount64();
    PollLogitech(ctx);
    assert(ctx.out.empty() && GetTickCount64() - start < 6 * 500 + 1000);

    // Garbage replies (wrong report id, short frames) are ignored.
    fake::Reset();
    {
        const std::wstring base = L"\\\\?\\hid#vid_046d&pid_c539&mi_02&col0";
        fake::Device& l = fake::Add(fake::Coll(base + L"2#8&1&0&0001#{x}", 0x046D, 0xC539, 0xFF00, 0x0002, 20, 20, 0),
                                    L"USB Receiver");
        l.onWrite = [](const std::vector<BYTE>& q, fake::Device& self) {
            self.input.push_back({ 0x20, q[1], q[2], q[3] });
            self.input.push_back({ 0x11, q[1] });
            self.input.push_back({ 0x10, q[1], 0x8F, q[2], q[3], 0x09, 0 });
        };
    }
    ctx = Context();
    PollLogitech(ctx);
    assert(ctx.out.empty());

    std::printf("Logitech driver tests passed (unified battery, charging, levels, voltage, HID++ 1.0, two mice, empty, "
                "other vendor, silent, garbage)\n");
    return 0;
}
