// Razer driver on simulated 90-byte feature reports (tests/fake_hid.h), framed as the driver documents
// from OpenRazer: [status][tid][2..3 remaining][4 protocol][5 size][6 class][7 id][8.. args][88 crc].
#include "fake_hid.h"

#include <cassert>
#include <cstdio>
#include <memory>

namespace {

struct Razer {
    BYTE tid = 0x1F;         // the transaction id the device answers to
    BYTE level = 200;        // 0..255
    BYTE charging = 0;
    BYTE status = 0x02;      // 02 ok, 04 offline, 05 not supported
    int busyReplies = 0;     // answers "busy" this many times first
    bool badCrc = false;
    BYTE otherTidStatus = 0x04;  // reply to other transaction ids (04 timeout); 0 = no reply at all
};

std::vector<BYTE> Reply(const std::vector<BYTE>& q, BYTE status, BYTE arg1, bool badCrc) {
    std::vector<BYTE> r(91, 0);
    BYTE* a = r.data() + 1;
    const BYTE* req = q.data() + 1;
    a[0] = status;
    for (int i = 1; i < 8; i++) a[i] = req[i];
    a[9] = arg1;  // args[1]
    BYTE crc = 0;
    for (int i = 2; i < 88; i++) crc ^= a[i];
    a[88] = badCrc ? (BYTE)(crc ^ 0xFF) : crc;
    return r;
}

fake::Device& Add(const std::wstring& path, USHORT pid, const std::wstring& product, Razer z) {
    fake::Device& d = fake::Add(fake::Coll(path, 0x1532, pid, 0x0001, 0x0002, 0, 0, 91), product);
    auto busy = std::make_shared<int>(z.busyReplies);
    d.onWrite = [z, busy](const std::vector<BYTE>& q, fake::Device& self) {
        const BYTE tid = q[2], cls = q[7], id = q[8];
        if (tid != z.tid) {  // wrong transaction id: usually the receiver reports a timeout
            if (z.otherTidStatus)
                self.features[0] = Reply(q, z.otherTidStatus, 0, false);
            else
                self.features.erase(0);
            return;
        }
        const BYTE arg = cls == 0x07 && id == 0x80 ? z.level : cls == 0x07 && id == 0x84 ? z.charging : 0;
        self.features[0] = Reply(q, *busy > 0 ? 0x01 : z.status, arg, z.badCrc);
        self.writes.push_back(q);  // the request the busy replies belong to
    };
    // While busy, each read shows "busy" once more; then the finished reply.
    d.onGetFeature = [z, busy](BYTE, fake::Device& self) {
        if (*busy <= 0 || self.writes.empty()) return;
        if (--*busy == 0) {
            const auto& q = self.writes.back();
            const BYTE arg = q[7] == 0x07 && q[8] == 0x80 ? z.level : q[7] == 0x07 && q[8] == 0x84 ? z.charging : 0;
            self.features[0] = Reply(q, z.status, arg, z.badCrc);
        }
    };
    return d;
}

PollContext Context() {
    PollContext ctx;
    for (const auto& [path, d] : fake::devices) ctx.colls.push_back(d.coll);
    return ctx;
}

}  // namespace

int main() {
    // A mouse on its dongle: 200/255 -> 78%, then the charging query.
    fake::Reset();
    Add(L"mouse", 0x00B7, L"Razer DeathAdder V3 Pro Wireless Receiver", Razer{ 0x1F, 200, 1 });
    PollContext ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 78 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"Razer DeathAdder V3 Pro" && ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].key == L"razer:00B7" && ctx.IsClaimed(0x1532, 0x00B7));
    // Only battery (07 80) and charging (07 84) queries were sent.
    for (const auto& q : fake::devices[L"mouse"].featuresSet) assert(q[7] == 0x07 && (q[8] == 0x80 || q[8] == 0x84));

    // 0 and 255 map to 0% and 100%.
    for (BYTE level : { 0, 255 }) {
        fake::Reset();
        Add(L"mouse", 0x00B7, L"Razer Mouse", Razer{ 0x1F, level, 0 });
        ctx = Context();
        PollRazer(ctx);
        assert(ctx.out.size() == 1 && ctx.out[0].percent == (level ? 100 : 0) && !ctx.out[0].charging);
    }

    // A wireless keyboard answers only to its documented transaction id 0x9F.
    fake::Reset();
    Add(L"kbd", 0x02BA, L"Razer BlackWidow V3 Mini", Razer{ 0x9F, 128, 0 });
    ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].kind == DeviceKind::Keyboard && ctx.out[0].percent == 50);
    for (const auto& q : fake::devices[L"kbd"].featuresSet) assert(q[2] == 0x9F);

    // Other models are probed with the known ids until one answers.
    fake::Reset();
    Add(L"mouse", 0x00B7, L"Razer Mouse", Razer{ 0x08, 51, 0 });
    ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 20);

    // Busy replies are waited out without resending.
    fake::Reset();
    {
        Razer z;
        z.busyReplies = 2;
        Add(L"mouse", 0x00B7, L"Razer Mouse", z);
    }
    ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 78);

    // The dongle reports the mouse as off: listed as disconnected, no level.
    fake::Reset();
    {
        Razer z;
        z.status = 0x04;
        Add(L"mouse", 0x00B7, L"Razer Mouse", z);
    }
    ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.size() == 1 && !ctx.out[0].connected && ctx.out[0].percent == -1);

    // Not supported, or a reply with a wrong checksum: nothing is reported or claimed.
    for (bool unsupported : { true, false }) {
        fake::Reset();
        Razer z;
        if (unsupported)
            z.status = z.otherTidStatus = 0x05;
        else {
            z.badCrc = true;
            z.otherTidStatus = 0;
        }
        Add(L"mouse", 0x00B7, L"Razer Mouse", z);
        ctx = Context();
        PollRazer(ctx);
        assert(ctx.out.empty() && !ctx.IsClaimed(0x1532, 0x00B7));
    }

    // The same mouse on its cable and its dongle is listed once; charging comes from the cable.
    fake::Reset();
    Add(L"dongle", 0x00B7, L"Razer DeathAdder V3 Pro Wireless Receiver", Razer{ 0x1F, 200, 0 });
    Add(L"cable", 0x00B6, L"Razer DeathAdder V3 Pro (Wired)", Razer{ 0x1F, 200, 1 });
    ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].charging);
    assert(ctx.IsClaimed(0x1532, 0x00B6) && ctx.IsClaimed(0x1532, 0x00B7));

    // Collections without the 91-byte feature report and other vendors are not opened.
    fake::Reset();
    fake::Add(fake::Coll(L"kbd-in", 0x1532, 0x00B7, 0x0001, 0x0006, 9, 0, 0));
    fake::Add(fake::Coll(L"other", 0x046D, 0xC539, 0xFF00, 0x0002, 20, 20, 91));
    ctx = Context();
    PollRazer(ctx);
    assert(ctx.out.empty() && fake::devices[L"kbd-in"].opens == 0 && fake::devices[L"other"].opens == 0);

    std::printf("Razer driver tests passed (level, charging, keyboard tid, tid probing, busy, offline, unsupported, "
                "crc, cable + dongle, filtering)\n");
    return 0;
}
