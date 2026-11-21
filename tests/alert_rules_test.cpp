// Standalone portable policy tests; no real wireless device required.
#include "../src/alert_rules.h"

#include <cassert>
#include <cstdio>
#include <map>

int main() {
    using namespace alerts;
    std::map<std::wstring, int> prefs;
    const std::wstring mouse = L"mouse:key";
    const std::wstring headset = L"headset:key";
    assert(EffectiveThreshold(prefs, mouse) == 15);
    assert(OverrideFor(prefs, mouse) == kUseDefault);
    prefs[mouse] = 20;
    prefs[headset] = kDisabled;
    assert(EffectiveThreshold(prefs, mouse) == 20);
    assert(EffectiveThreshold(prefs, headset) == kDisabled);
    assert(EffectiveThreshold(prefs, L"other:key") == 15);
    assert(ValidOverride(kDisabled));
    for (int n : kThresholds) assert(ValidOverride(n));
    assert(!ValidOverride(0) && !ValidOverride(51) && !ValidOverride(kUseDefault));

    DeviceStatus s;
    s.key = mouse;
    s.percent = 21;
    bool warned = false;
    assert(!ShouldNotify(s, 20, warned));
    s.percent = 20;
    assert(ShouldNotify(s, 20, warned));
    assert(warned);
    assert(!ShouldNotify(s, 20, warned));  // same reading must not notify again
    s.percent = 25;
    assert(!ShouldNotify(s, 20, warned));  // hysteresis prevents flapping
    s.percent = -1;
    assert(!ShouldNotify(s, 20, warned));
    assert(warned);                        // unknown reading preserves latch
    s.percent = 10;
    assert(!ShouldNotify(s, 20, warned));  // returning from unknown is not a new alert
    s.asleep = true;
    assert(!ShouldNotify(s, 20, warned));
    s.asleep = false;
    s.connected = false;
    assert(!ShouldNotify(s, 20, warned));
    s.connected = true;
    assert(!ShouldNotify(s, 20, warned));
    s.cached = true;
    s.percent = 90;
    s.charging = true;
    assert(!s.HasFreshBattery());
    assert(!ShouldNotify(s, 20, warned) && warned);  // a cached high/charging sample can't rearm the alert
    warned = false;
    s.percent = 0;
    s.charging = false;
    assert(!ShouldNotify(s, 20, warned) && !warned);
    s.cached = false;
    assert(s.HasFreshBattery());
    assert(ShouldNotify(s, 20, warned));  // a new 0% measurement still alerts
    s.percent = 31;
    assert(!ShouldNotify(s, 20, warned));  // armed again only > threshold + 10
    assert(!warned);
    s.percent = 19;
    assert(ShouldNotify(s, 20, warned));

    s.charging = true;
    assert(!ShouldNotify(s, 20, warned));
    assert(!warned);
    s.charging = false;
    assert(ShouldNotify(s, 20, warned));

    assert(!ShouldNotify(s, kDisabled, warned));
    assert(!warned);
    assert(ShouldNotify(s, 20, warned));   // re-enabled: new low battery alert
    s.percent = 105;
    warned = false;
    assert(!ShouldNotify(s, 20, warned));  // invalid data never alerts
    s.percent = 0;
    assert(ShouldNotify(s, 20, warned));   // 0% is a valid critical reading
    DeviceStatus headphones;
    headphones.key = headset;
    headphones.percent = 9;
    bool secondWarned = false;
    assert(ShouldNotify(headphones, 15, secondWarned));
    assert(!ShouldNotify(headphones, 15, secondWarned));
    assert(warned);                        // the mouse and headphones have independent latches

    // The red display level follows the device threshold; default and disabled alerts keep 15%.
    std::map<std::wstring, int> display = { { L"a", 30 }, { L"b", kDisabled } };
    assert(DisplayThreshold(display, L"a") == 30);
    assert(DisplayThreshold(display, L"b") == kDefaultThreshold);
    assert(DisplayThreshold(display, L"none") == kDefaultThreshold);

    // Charge reminder: once at the target while on power, re-armed by unplugging.
    DeviceStatus pad;
    pad.percent = 79;
    pad.charging = true;
    bool charged = false;
    assert(!ShouldNotifyCharged(pad, 80, charged));
    pad.percent = 80;
    assert(ShouldNotifyCharged(pad, 80, charged) && charged);
    pad.percent = 85;
    assert(!ShouldNotifyCharged(pad, 80, charged));  // once
    pad.asleep = true;
    assert(!ShouldNotifyCharged(pad, 80, charged) && charged);  // stale readings keep the latch
    pad.asleep = false;
    pad.charging = false;
    assert(!ShouldNotifyCharged(pad, 80, charged) && !charged);  // unplugged: re-armed
    pad.charging = true;
    assert(ShouldNotifyCharged(pad, 80, charged));
    // 100%: a device that only says "full" counts too; off and invalid targets never fire.
    DeviceStatus full;
    full.percent = 99;
    full.full = true;
    bool fullLatch = false;
    assert(ShouldNotifyCharged(full, 100, fullLatch));
    bool off = true;
    assert(!ShouldNotifyCharged(full, 0, off) && !off);
    assert(!ShouldNotifyCharged(full, 85, off));
    assert(ValidChargeTarget(90) && !ValidChargeTarget(95));

    std::puts("Smart Battery Alerts policy tests passed");
}
