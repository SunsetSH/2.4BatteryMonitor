// Runs every driver in order. Vendor drivers go first and claim the devices they handle; the
// generic fallbacks then only look at what is left. Drivers of the vendors listed in driver_lanes.h
// run in parallel lanes (docs/adr/0007-parallel-poll-lanes.md); a slow driver that keeps finding
// nothing is skipped for a while (driver_backoff.h).
#include "devices.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <optional>
#include <thread>

#include "app_settings.h"
#include "diagnostics.h"
#include "driver_backoff.h"
#include "driver_lanes.h"
#include "drivers/drivers.h"
#include "hid.h"
#include "ignore_list.h"

namespace {

struct Driver {
    const char* name;
    void (*poll)(PollContext&);
};

// Vendor drivers in chain order. Where two drivers share a vendor id, the comment says why the first
// one must come first.
const Driver kChain[] = {
    { "Glorious", PollGlorious },
    { "Aula", PollAula },  // before Kysona (shares the 3554 vendor id) and SONiX (shares 05AC:024F)
    { "AulaMouse", PollAulaMouse },
    { "Eweadn", PollEweadn },  // EWEADN S7 / S9 mice on 372E:103D / 1027 (AULA keeps 372E:106C)
    { "Dareu", PollDareu },
    { "Zenith", PollZenith },
    { "Darmoshark", PollDarmoshark },
    { "DarmosharkNrf", PollDarmosharkNrf },
    { "AtkSerial", PollAtkSerial },  // before Kysona, which shares the ATK vendor id
    { "AtkKeyboard", PollAtkKeyboard },
    { "RedragonEvision", PollRedragonEvision },
    { "Pulsar", PollPulsar },  // before Kysona, which also scans the 3710 vendor id
    { "AtkBitmouse", PollAtkBitmouse },  // before Kysona, which would probe the NK dongle with the wrong protocol
    { "Chimera", PollChimera },  // Chimera V2 mouse (3554:F512), before Kysona which shares the 3554 vendor id
    { "Kysona", PollKysona },
    { "Keychron", PollKeychron },
    { "Cherry", PollCherry },
    { "Nuphy", PollNuphy },
    { "Arbiter", PollArbiter },  // before Lamzu, which shares the 373E vendor id
    { "Lamzu", PollLamzu },
    { "Endgame", PollEndgame },
    { "EndgameWe", PollEndgameWe },
    { "Vaxee", PollVaxee },
    { "Wlmouse", PollWlmouse },
    { "Finalmouse", PollFinalmouse },
    { "Scyrox", PollScyrox },
    { "Ninjutso", PollNinjutso },
    { "Sprime", PollSprime },
    { "Rawm", PollRawm },
    { "Akko", PollAkko },
    { "Rongyuan", PollRongyuan },
    { "Sonix", PollSonix },
    { "ViaDongle", PollViaDongle },
    { "Semico", PollSemico },
    { "AjazzAk992", PollAjazzAk992 },
    { "SonyInzone", PollSonyInzone },
    { "Fractal", PollFractal },
    { "Jabra", PollJabra },
    { "Jbl", PollJbl },
    { "Rog", PollRog },
    { "Zowie", PollZowie },
    { "RoyalKludge", PollRoyalKludge },
    { "Mchose", PollMchose },
    { "MchoseA5", PollMchoseA5 },
    { "Rapoo", PollRapoo },
    { "Areson", PollAreson },
    { "MchoseG3", PollMchoseG3 },
    { "Msi", PollMsi },
    { "Machenike", PollMachenike },  // before Ajazz in PollMiscMice, which shares the L7's 248A vendor id
    { "Niceboy", PollNiceboy },  // exact 248A:FA02 match, also before the Ajazz / Delux pollers
    { "MchoseModern", PollMchoseModern },
    { "MchoseHeadset", PollMchoseHeadset },
    { "Vgn", PollVgn },
    { "MchoseKeyboard", PollMchoseKeyboard },  // 3837:3033 receiver, before the generic fallbacks
    { "MiscMice", PollMiscMice },
    { "Fantech", PollFantech },
    { "RazerHeadsets", PollRazerHeadsets },  // before the generic Razer driver, which shares the vendor id
    { "BarracudaPro", PollBarracudaPro },  // before the generic Razer driver as well
    { "HyperX", PollHyperX },
    { "LogitechG435", PollLogitechG435 },  // before the HID++ driver, which shares the vendor id
    { "Astro", PollAstro },
    { "Logitech", PollLogitech },
    { "Razer", PollRazer },
    { "SteelSeriesElite", PollSteelSeriesElite },
    { "SteelSeries", PollSteelSeries },
    { "Audeze", PollAudeze },
    { "Endorfy", PollEndorfy },
    { "TurtleBeach", PollTurtleBeach },
    { "Akg", PollAkg },
    { "Nzxt", PollNzxt },
    { "Plantronics", PollPlantronics },
    { "Corsair", PollCorsair },
    { "CoolerMaster", PollCoolerMaster },
    { "PlayStation", PollPlayStation },
    { "SwitchPro", PollSwitchPro },
    { "SteamController", PollSteamController },
    { "GameSir", PollGameSir },
    { "Flydigi", PollFlydigi },
    { "GameSirCyclone", PollGameSirCyclone },
    { "GameSirBle", PollGameSirBle },  // Cyclone Pro: battery over BLE; claims its dongle before the XInput fallback
    { "8BitDo", Poll8BitDo },
    { "Beitong", PollBeitong },  // before the XInput fallback, which only sees the receiver as wired
    { "AirPods", PollAirPods },  // before the Bluetooth fallback, which has no level for AirPods
    { "PhoneLink", PollPhoneLink },
    { "GalaxyBuds", PollGalaxyBuds },  // before the Bluetooth fallback, which skips devices already listed by name
    { "NothingEar", PollNothingEar },  // before the Bluetooth fallback, which skips devices already listed by name
    { "Moondrop", PollMoondrop },
    { "Darkrim", PollDarkrim },
    { "Alienware", PollAlienware },
    { "Maono", PollMaono },  // MAONO PD200W receiver (352F:040E), experimental battery query
};

// After every vendor driver, on the merged results.
const Driver kFallbacks[] = {
    { "Bluetooth", PollBluetooth },  // before the generic HID check: Windows' own level wins for the same device
    { "GenericHid", PollGenericHid },
    { "XInput", PollXInput },
};

constexpr size_t kChainSize = sizeof(kChain) / sizeof(kChain[0]);

// Removes the collections of ignored devices and returns their paths: PollBluetooth needs them to
// skip the same devices in Windows' own Bluetooth battery levels.
std::vector<std::wstring> FilterIgnored(std::vector<HidColl>& colls, const std::vector<ignore_list::Id>& ignored) {
    std::vector<std::wstring> removed;
    if (ignored.empty()) return removed;
    for (const auto& c : colls)
        if (ignore_list::Contains(ignored, c.vid, c.pid)) removed.push_back(c.path);
    colls.erase(std::remove_if(colls.begin(), colls.end(),
                               [&](const HidColl& c) { return ignore_list::Contains(ignored, c.vid, c.pid); }),
                colls.end());
    return removed;
}

const lanes::Lane* LaneOf(const char* driver) {
    for (const auto& lane : lanes::kLanes)
        for (const char* d : lane.drivers)
            if (d && !strcmp(d, driver)) return &lane;
    return nullptr;
}

// What one driver found, kept apart so the merge can follow the chain order.
struct Slice {
    std::vector<DeviceStatus> out;
    std::set<std::pair<USHORT, USHORT>> claims;
    bool done = false;
};

class Poll {
public:
    Poll(const PollOptions& o, std::vector<ignore_list::Id> ignored)
        : options_(o), ignored_(std::move(ignored)), slices_(kChainSize) {}

    // Runs drivers [index...] that belong to `lane` (null: the main chain) in one context.
    void RunChain(const lanes::Lane* lane, std::vector<HidColl> colls) {
        PollContext ctx;
        ctx.colls = std::move(colls);
        ctx.waitForInitialControllers = options_.waitForInitialControllers;
        for (size_t i = 0; i < kChainSize; i++) {
            if (options_.parallel && LaneOf(kChain[i].name) != lane) continue;
            RunOne(kChain[i], i, ctx, lane ? lane->name : "");
        }
    }

    void RunFallbacks(PollContext& ctx) {
        for (const auto& d : kFallbacks) {
            size_t before = ctx.out.size();
            ULONGLONG t0 = GetTickCount64();
            d.poll(ctx);
            Timing(d.name, "", GetTickCount64() - t0, (int)(ctx.out.size() - before), false);
        }
    }

    // Every finished slice in chain order.
    PollContext Merged(std::vector<HidColl> colls) {
        std::lock_guard<std::mutex> lock(mutex_);
        PollContext ctx;
        ctx.colls = std::move(colls);
        ctx.waitForInitialControllers = options_.waitForInitialControllers;
        for (const auto& s : slices_) {
            ctx.out.insert(ctx.out.end(), s.out.begin(), s.out.end());
            ctx.claimed.insert(s.claims.begin(), s.claims.end());
        }
        return ctx;
    }

private:
    void RunOne(const Driver& d, size_t index, PollContext& ctx, const char* lane) {
        const ULONGLONG now = GetTickCount64();
        if (options_.schedule) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!options_.schedule->ShouldRun(d.name, now)) {
                // Keep what it claimed last time, so no other driver reports those devices meanwhile.
                for (const auto& c : options_.schedule->LastClaims(d.name)) ctx.Claim(c.first, c.second);
                slices_[index].claims = options_.schedule->LastClaims(d.name);
                Timing(d.name, lane, 0, 0, true);
                return;
            }
        }
        const size_t before = ctx.out.size();
        const auto claimedBefore = ctx.claimed;
        d.poll(ctx);
        const ULONGLONG ms = GetTickCount64() - now;
        Slice slice;
        slice.out.assign(ctx.out.begin() + before, ctx.out.end());
        for (const auto& c : ctx.claimed)
            if (!claimedBefore.count(c)) slice.claims.insert(c);
        slice.done = true;
        std::lock_guard<std::mutex> lock(mutex_);
        if (options_.schedule) options_.schedule->Ran(d.name, now, ms, slice.out.size(), slice.claims, options_.intervalMs);
        Timing(d.name, lane, (unsigned)ms, (int)slice.out.size(), false);
        const bool found = !slice.out.empty();
        slices_[index] = std::move(slice);
        if (found) PartialLocked();
    }

    void Timing(const char* name, const char* lane, ULONGLONG ms, int found, bool skipped) {
        if (options_.timings) options_.timings->push_back({ name, lane, (unsigned)ms, found, skipped });
    }

    // Reports what was found so far, at most every 150 ms.
    void PartialLocked() {
        if (!options_.partial) return;
        const ULONGLONG now = GetTickCount64();
        if (now - lastPartial_ < 150) return;
        lastPartial_ = now;
        std::vector<DeviceStatus> so;
        for (const auto& s : slices_) so.insert(so.end(), s.out.begin(), s.out.end());
        ignore_list::DropIgnored(so, ignored_);
        options_.partial(std::move(so));
    }

    const PollOptions& options_;
    const std::vector<ignore_list::Id> ignored_;
    std::mutex mutex_;
    std::vector<Slice> slices_;
    ULONGLONG lastPartial_ = 0;
};

}  // namespace

std::vector<DeviceStatus> PollWlmouseDevices() {
    PollContext ctx;
    ctx.colls = EnumUsbHidForVendor(0x36A7);
    FilterIgnored(ctx.colls, ignore_list::Parse(app_settings::LoadText(L"IgnoreDevices")));
    PollGlorious(ctx);  // The alternate protocol for the YING receiver.
    PollWlmouse(ctx);
    return std::move(ctx.out);
}

std::vector<DeviceStatus> PollAllDevices(const PollOptions& options) {
    std::vector<HidColl> colls = EnumHid();
    const auto ignored = ignore_list::Parse(app_settings::LoadText(L"IgnoreDevices"));
    std::vector<std::wstring> ignoredPaths = FilterIgnored(colls, ignored);
    Poll poll(options, ignored);
    if (!options.parallel) {
        poll.RunChain(nullptr, colls);
    } else {
        // Each lane sees only its vendor's collections; the main chain sees everything else.
        auto ofVid = [&](USHORT vid) {
            std::vector<HidColl> out;
            for (const auto& c : colls)
                if (c.vid == vid) out.push_back(c);
            return out;
        };
        std::vector<HidColl> rest;
        for (const auto& c : colls) {
            bool laneVid = false;
            for (const auto& lane : lanes::kLanes) laneVid |= lane.vid && lane.vid == c.vid;
            if (!laneVid) rest.push_back(c);
        }
        std::vector<std::thread> threads;
        for (const auto& lane : lanes::kLanes) {
            std::vector<HidColl> own = lane.vid ? ofVid(lane.vid) : std::vector<HidColl>{};
            if (lane.vid && own.empty()) continue;  // nothing of this vendor is plugged in
            threads.emplace_back([&poll, &lane, own = std::move(own)]() mutable {
                // WinRT and COM based drivers (BLE adverts, Bluetooth) need an apartment on this thread.
                const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                poll.RunChain(&lane, std::move(own));
                if (SUCCEEDED(com)) CoUninitialize();
            });
        }
        poll.RunChain(nullptr, std::move(rest));
        for (auto& t : threads) t.join();
    }
    PollContext ctx = poll.Merged(colls);
    ctx.ignoredPaths = std::move(ignoredPaths);
    poll.RunFallbacks(ctx);
    ignore_list::DropIgnored(ctx.out, ignored);
    return std::move(ctx.out);
}

std::wstring DumpHidCollections() {
    std::wstring out = L"VID:PID    IF  UsagePg:Usage  In  Out Feat  Product\n";
    const auto colls = EnumHid();
    for (const auto& c : colls) {
        wchar_t line[160];
        swprintf(line, 160, L"%04X:%04X  %2d  %04X:%04X    %3u %3u %3u   ", c.vid, c.pid, c.iface, c.page(), c.usage(), c.inLen(), c.outLen(),
                 c.featLen());
        out += line + HidProduct(c) + L"\n";
    }
    out += DumpExtras(colls);  // unopenable HID collections, non-HID USB devices, Bluetooth devices
    return out;
}
