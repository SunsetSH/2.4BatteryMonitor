#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <deque>

namespace {

std::deque<std::vector<BYTE>> replies;
std::vector<std::wstring> opened;

HidColl SwordReceiver() {
    HidColl c;
    c.path = L"battery";
    c.vid = 0x36A7;
    c.pid = 0xA878;
    c.iface = 2;
    c.caps.UsagePage = 0xFFFF;
    c.caps.FeatureReportByteLength = 65;
    return c;
}

std::vector<BYTE> Reply(BYTE status, BYTE level, BYTE charging = 0) {
    return { 0, status, 0, 2, 2, 0, 0x83, charging, level };
}

}  // namespace

std::wstring Hex4(USHORT v) {
    wchar_t buf[8];
    swprintf(buf, 8, L"%04X", v);
    return buf;
}

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    opened.push_back(c.path);
    return true;
}

void HidDev::Close() {}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    std::vector<BYTE> expected(65);
    expected[3] = expected[4] = 2;
    expected[6] = 0x83;
    assert(data == expected);
    return true;
}

bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    assert(id == 0);
    if (replies.empty()) return false;
    out = std::move(replies.front());
    replies.pop_front();
    return true;
}

int main() {
    const HidColl receiver = SwordReceiver();
    HidColl keyboard = receiver;
    keyboard.path = L"keyboard";
    keyboard.iface = 1;
    keyboard.caps.Usage = 1;
    HidColl wrongInterface = receiver;
    wrongInterface.path = L"wrong interface";
    wrongInterface.iface = 1;
    HidColl wrongSize = receiver;
    wrongSize.path = L"wrong report size";
    wrongSize.caps.FeatureReportByteLength = 66;

    PollContext ctx;
    ctx.colls = {keyboard, wrongInterface, wrongSize, receiver};
    auto wrongId = Reply(0xA1, 97);
    wrongId[0] = 1;
    replies = {wrongId, Reply(0xA2, 0, 1)};
    PollWlmouse(ctx);
    assert(opened == std::vector<std::wstring>{L"battery"});
    assert(ctx.out.size() == 1);
    const auto& battery = ctx.out.front();
    assert(battery.key == L"wlmouse:A878" && battery.via == L"2.4 GHz receiver");
    assert(battery.percent == 0 && battery.charging && !battery.asleep && battery.graceSec == 1800);

    ctx = {};
    ctx.colls = {receiver};
    replies = {Reply(0xA0, 99), Reply(0xA0, 99)};
    PollWlmouse(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].percent == -1);

    ctx = {};
    ctx.colls = {receiver};
    replies = {Reply(0xA0, 99), Reply(0xA1, 52)};
    PollWlmouse(ctx);
    assert(ctx.out.size() == 1 && !ctx.out[0].asleep && ctx.out[0].percent == 52);

    ctx = {};
    ctx.colls = {receiver};
    replies = {Reply(0xA1, 101)};
    PollWlmouse(ctx);
    assert(ctx.out.empty() && ctx.claimed.empty());
    std::puts("WLMouse driver tests passed");
}
