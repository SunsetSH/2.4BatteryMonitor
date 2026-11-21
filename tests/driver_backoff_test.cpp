// Backoff of slow drivers that find nothing.
#include "../src/driver_backoff.h"

#include <cassert>
#include <cstdio>

using namespace backoff;

int main() {
    Schedule s;
    const uint64_t minute = 60 * 1000;
    assert(s.ShouldRun("rapoo", 0));

    // One slow empty run: still polled next time.
    s.Ran("rapoo", 0, 4000, 0, { { 0x24AE, 0x1417 } }, minute);
    assert(s.ShouldRun("rapoo", 1) && s.Misses("rapoo") == 1);

    // Second miss: skipped for one interval, claims are kept for the skipped polls.
    s.Ran("rapoo", minute, 4000, 0, { { 0x24AE, 0x1417 } }, minute);
    assert(!s.ShouldRun("rapoo", minute + 1000) && s.ShouldRun("rapoo", 2 * minute));
    assert(s.LastClaims("rapoo").count({ 0x24AE, 0x1417 }));

    // Then x2, x4 ... up to ten minutes.
    s.Ran("rapoo", 2 * minute, 4000, 0, {}, minute);
    assert(!s.ShouldRun("rapoo", 3 * minute + 59000) && s.ShouldRun("rapoo", 4 * minute));
    uint64_t now = 4 * minute;
    for (int i = 0; i < 10; i++) s.Ran("rapoo", now, 4000, 0, {}, minute);
    assert(!s.ShouldRun("rapoo", now + kMaxSkipMs - 1) && s.ShouldRun("rapoo", now + kMaxSkipMs));

    // A device found, or a fast run, clears the count.
    s.Ran("rapoo", now, 4000, 1, {}, minute);
    assert(s.Misses("rapoo") == 0 && s.ShouldRun("rapoo", now));
    s.Ran("areson", 0, 3800, 0, {}, minute);
    s.Ran("areson", 0, 3800, 0, {}, minute);
    assert(!s.ShouldRun("areson", 1));
    s.Reset();  // plugged in, resumed, "Refresh now"
    assert(s.ShouldRun("areson", 1) && s.Misses("areson") == 0);
    s.Ran("fast", 0, 20, 0, {}, minute);
    s.Ran("fast", 0, 20, 0, {}, minute);
    assert(s.ShouldRun("fast", 1) && s.Misses("fast") == 0);
    assert(s.LastClaims("unknown").empty());

    std::printf("driver_backoff_test passed\n");
    return 0;
}
