// Device report text and the GitHub issue URL.
#include "../src/report_text.h"

#include <cassert>
#include <cstdio>

using namespace report_text;

int main() {
    assert(UrlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    assert(UrlEncode("Az09-_.~") == "Az09-_.~");

    const std::wstring dump =
        L"VID:PID    IF  Product\n046D:C539   2  USB Receiver\n1532:00B7   0  Razer\n046D:C539   1  USB Receiver\n";
    assert(DumpFor(dump, 0x046D, 0xC539) ==
           L"VID:PID    IF  Product\n046D:C539   2  USB Receiver\n046D:C539   1  USB Receiver\n");
    assert(DumpFor(dump, 0, 0) == dump);
    assert(DumpFor(dump, 0x1234, 0x0001) == L"VID:PID    IF  Product\n");

    DeviceStatus s;
    s.name = L"G502 X";
    s.key = L"logi:4099";
    s.via = L"Logitech receiver";
    s.vid = 0x046D;
    s.pid = 0xC539;
    s.percent = 64;
    s.charging = true;
    std::wstring body = Body(&s, L"1.2.3", L"10.0.26200", L"x\n");
    assert(body.find(L"**VID:PID:** 046D:C539") != std::wstring::npos);
    assert(body.find(L"64%, charging") != std::wstring::npos);
    assert(body.find(L"```\nx\n```") != std::wstring::npos);
    assert(Body(nullptr, L"1", L"10", L"").find(L"Device model") != std::wstring::npos);

    // Long dumps are cut at line ends, the note is added once and the body keeps its closing fence.
    std::wstring longDump;
    for (int i = 0; i < 400; i++) longDump += L"1234:5678   0  some collection line " + std::to_wstring(i) + L"\n";
    auto make = [&](const std::wstring& d) { return std::string("```\n") + std::string(d.begin(), d.end()) + "```\n"; };
    bool cut = false;
    std::string url = IssueUrl("o/r", "t", longDump, make, 2000, cut);
    assert(cut && url.size() <= 2000);
    assert(url.find("https://github.com/o/r/issues/new?title=t&body=") == 0);
    assert(url.find("cut%2C%20run") != std::string::npos);
    assert(url.rfind("%60%60%60%0A") == url.size() - 12);  // still ends with the closing fence
    url = IssueUrl("o/r", "t", L"short\n", make, 2000, cut);
    assert(!cut);

    std::printf("report_text_test passed\n");
    return 0;
}
