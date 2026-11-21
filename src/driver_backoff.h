#pragma once
// Backoff for drivers that keep spending their whole timeout without finding anything (a sleeping
// mouse that never answers, a receiver whose device is switched off). Each such poll wakes the
// radio and delays the rest of the chain. Portable, tested in tests/driver_backoff_test.cpp.
//
// Rule: a driver run that took at least kSlowMs and reported no device is a miss. From the second
// miss in a row the driver is skipped for interval x 1, 2, 4 ... polls, at most kMaxSkipMs. A run that
// finds a device, or Reset() (device plugged in, resume, "Refresh now"), clears the count. A skipped
// driver keeps the vendor / product ids it claimed last time, so the generic fallbacks do not report
// those devices without the driver's knowledge.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace backoff {

constexpr uint64_t kSlowMs = 300;
constexpr uint64_t kMaxSkipMs = 10 * 60 * 1000;

using Claims = std::set<std::pair<unsigned short, unsigned short>>;

struct DriverState {
    int misses = 0;
    uint64_t skipUntil = 0;
    Claims claims;  // what the driver claimed in its last run
};

class Schedule {
public:
    bool ShouldRun(const std::string& driver, uint64_t now) const {
        auto it = state_.find(driver);
        return it == state_.end() || now >= it->second.skipUntil;
    }
    const Claims& LastClaims(const std::string& driver) const {
        static const Claims none;
        auto it = state_.find(driver);
        return it == state_.end() ? none : it->second.claims;
    }
    // Records a run that took `ms` and reported `found` devices; `intervalMs` is the poll interval.
    void Ran(const std::string& driver, uint64_t now, uint64_t ms, size_t found, Claims claims, uint64_t intervalMs) {
        DriverState& s = state_[driver];
        s.claims = std::move(claims);
        if (found || ms < kSlowMs) {
            s.misses = 0;
            s.skipUntil = 0;
            return;
        }
        ++s.misses;
        if (s.misses < 2) return;
        uint64_t skip = intervalMs;
        for (int i = 2; i < s.misses && skip < kMaxSkipMs; i++) skip *= 2;
        if (skip > kMaxSkipMs) skip = kMaxSkipMs;
        s.skipUntil = now + skip;
    }
    void Reset() {
        for (auto& [name, s] : state_) {
            s.misses = 0;
            s.skipUntil = 0;
        }
    }
    int Misses(const std::string& driver) const {
        auto it = state_.find(driver);
        return it == state_.end() ? 0 : it->second.misses;
    }

private:
    std::map<std::string, DriverState> state_;
};

}  // namespace backoff
