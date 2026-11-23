// IgnoreDevices parsing shared by the poll and the settings window.
#include "../src/ignore_list.h"

#include <cassert>
#include <cstdio>

int main() {
    using ignore_list::Id;

    // The settings window's own format.
    auto ids = ignore_list::Parse(L"046D:C52B,373E:0016");
    assert(ids.size() == 2 && ids[0] == Id(0x046D, 0xC52B) && ids[1] == Id(0x373E, 0x0016));

    // Hand-edited registry values: lower case, short ids, spaces and semicolons. The poll always
    // accepted these; the settings window used to drop them and erase them on the next save.
    ids = ignore_list::Parse(L" 46d:c52b ; 3554:F505  2dc8:6012,");
    assert(ids.size() == 3 && ids[0] == Id(0x046D, 0xC52B) && ids[1] == Id(0x3554, 0xF505) &&
           ids[2] == Id(0x2DC8, 0x6012));
    assert(ignore_list::Join(ids) == L"046D:C52B,3554:F505,2DC8:6012");

    // Garbage, five-digit halves and missing parts are skipped; the rest of the list still counts.
    ids = ignore_list::Parse(L"foo,12345:0001,046D:,:C52B,046D-C52B,046D:C52B:1,1915:0723");
    assert(ids.size() == 1 && ids[0] == Id(0x1915, 0x0723));

    // A repeated entry counts once, whatever its spelling.
    ids = ignore_list::Parse(L"046D:C52B,046d:c52b");
    assert(ids.size() == 1);

    assert(ignore_list::Parse(L"").empty() && ignore_list::Parse(L" ,;").empty());
    assert(ignore_list::Format({ 0, 0xFFFF }) == L"0000:FFFF");
    assert(ignore_list::Contains(ignore_list::Parse(L"046D:C52B"), 0x046D, 0xC52B));
    assert(!ignore_list::Contains(ignore_list::Parse(L"046D:C52B"), 0x046D, 0xC52C));

    // What swscanf "%x" used to accept in the poll: "0x" prefixes and leading zeros. Values over 16 bits
    // and vendor id 0 ("no id" in DeviceStatus, would match every device without one) are not ids.
    ids = ignore_list::Parse(L"0x046d:0XC52B,0000373E:00016,1:10000,0000:1234");
    assert(ids.size() == 2 && ids[0] == Id(0x046D, 0xC52B) && ids[1] == Id(0x373E, 0x0016));

    // One entry per line in a hand-edited value.
    ids = ignore_list::Parse(L"046D:C52B\r\n373E:0016\n");
    assert(ids.size() == 2);

    // Results: ignored ids go, devices without an id and other ids stay.
    struct Status {
        unsigned short vid, pid;
        int tag;
    };
    std::vector<Status> results{ { 0x046D, 0xC52B, 1 }, { 0, 0, 2 }, { 0x046D, 0xC52C, 3 }, { 0x045E, 0x0B13, 4 } };
    ignore_list::DropIgnored(results, ignore_list::Parse(L"046D:C52B,045E:0B13"));
    assert(results.size() == 2 && results[0].tag == 2 && results[1].tag == 3);
    ignore_list::DropIgnored(results, {});
    assert(results.size() == 2);

    // Round trip: what the settings window saves reads back the same.
    const auto saved = ignore_list::Parse(L"1:2,abcd:EF01");
    assert(ignore_list::Parse(ignore_list::Join(saved)) == saved);

    std::printf("ignore_list_test: ok\n");
    return 0;
}
