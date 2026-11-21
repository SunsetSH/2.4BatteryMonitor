#include "../src/drivers/drivers.h"
#include "../src/json_output.h"
#include <cassert>
#include <cstdio>

namespace {

bool headsetAvailable = true;
bool headsetRequested = false;
std::vector<BYTE> keyboardRequest;

HidColl Collection(USHORT vid, USHORT pid, int iface, USHORT page, USHORT usage,
                   USHORT input, USHORT output, USHORT feature) {
    HidColl c;
    c.vid = vid;
    c.pid = pid;
    c.iface = iface;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = input;
    c.caps.OutputReportByteLength = output;
    c.caps.FeatureReportByteLength = feature;
    return c;
}

PollContext PollSetup() {
    PollContext ctx;
    ctx.colls = {
        Collection(0x36A7, 0xA878, 2, 0xFFFF, 0, 0, 0, 65),
        Collection(0x320F, 0x50C3, 1, 0xFF1C, 0x0092, 64, 64, 0),
        Collection(0x3738, 0x0072, 3, 0xFF13, 1, 62, 62, 0)
    };
    headsetRequested = false;
    PollWlmouse(ctx);
    PollAjazzAk992(ctx);
    PollEndorfy(ctx);
    return ctx;
}

}

bool HidDev::Open(const HidColl& c) { c_ = c; return true; }
bool HidDev::OpenFeatureOnly(const HidColl& c) { return Open(c); }
void HidDev::Close() {}
void HidDev::Flush() {}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    assert(c_.pid == 0xA878 && data.size() == 65 && data[6] == 0x83);
    return true;
}

bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    assert(c_.pid == 0xA878 && id == 0);
    out = {0, 0xA1, 0, 2, 2, 0, 0x83, 0, 69};
    return true;
}

bool HidDev::Write(const std::vector<BYTE>& bytes, int) {
    if (c_.pid == 0x50C3) {
        assert(bytes.size() == 4 && bytes[0] == 4 && bytes[3] == 0x1A);
        keyboardRequest = bytes;
        return true;
    }
    assert(c_.pid == 0x0072 && bytes.size() == 62);
    return headsetRequested = headsetAvailable;
}

int HidDev::Read(std::vector<BYTE>& out, int) {
    assert(c_.pid == 0x50C3 && !keyboardRequest.empty());
    out = {4, keyboardRequest[1], keyboardRequest[2], 0x1A, 0, 0, 0, 0, 87, 1};
    return (int)out.size();
}

bool HidDev::GetInputReport(BYTE id, std::vector<BYTE>& out) {
    assert(c_.pid == 0x0072 && id == 7);
    if (!headsetRequested) return false;
    out = {7, 9, 0x80, 0x05, 0x5D, 5, 0, 0xD6, 0x0C, 0, 0, 56};
    return true;
}

std::wstring Hex4(USHORT v) {
    wchar_t buf[5];
    swprintf(buf, 5, L"%04X", v);
    return buf;
}

int main() {
    auto ctx = PollSetup();
    assert(ctx.out.size() == 3 && ctx.claimed.size() == 3);
    assert(ctx.out[0].kind == DeviceKind::Mouse && ctx.out[0].percent == 69);
    assert(ctx.out[1].kind == DeviceKind::Keyboard && ctx.out[1].percent == 87 && ctx.out[1].charging);
    assert(ctx.out[2].kind == DeviceKind::Headset && ctx.out[2].percent == 56);
    const auto json = DevicesToJson(ctx.out);
    for (const char* key : {"wlmouse:A878", "ajazz-ak992:320F:50C3", "endorfy:viro"})
        assert(json.find(key) != json.npos);
    headsetAvailable = false;
    ctx = PollSetup();
    assert(ctx.out.size() == 2 && ctx.out[0].percent == 69 && ctx.out[1].percent == 87);
    std::puts("Mixed mouse, keyboard and headset reports verified");
}
