#pragma once
#include <functional>
#include <string>
#include <vector>

enum class DeviceKind { Other, Mouse, Headset, Keyboard, Gamepad, Phone, Microphone };

// One battery-powered device, as seen during a single poll.
struct DeviceStatus {
    std::wstring key;          // stable identity across polls (used for the tray icon id)
    std::wstring name;         // human readable name
    std::wstring via;          // "2.4 GHz receiver", "Bluetooth", "USB cable", ...
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;      // 0..100, or -1 when unknown right now
    std::wstring detail;       // extra driver note for the tooltip ("docked"); earbud levels go below
    int  left       = -1;      // earbuds: left bud, right bud and case levels, -1 = not reported
    int  right      = -1;
    int  casePct    = -1;
    std::wstring levelText;    // set when the device only reports coarse levels ("Low", "Full"...)
    bool charging   = false;
    bool full       = false;   // on power and fully charged (charging may also be set)
    bool asleep     = false;   // receiver answered but the device is sleeping (keep last value)
    bool connected  = true;    // false: receiver present, device switched off (hidden)
    int  millivolts = 0;       // battery voltage when reported (0 = unknown)
    int  graceSec   = 0;       // keep showing the last value this long if the device stops answering
    unsigned short vid = 0;    // USB / Bluetooth HID vendor id when known (picks the brand color)
    unsigned short pid = 0;
    bool cached = false;       // known percentage reused without a new measurement

    bool HasFreshBattery() const { return connected && !asleep && !cached && percent >= 0 && percent <= 100; }
};

#ifdef _WIN32
namespace backoff { class Schedule; }

// How long one driver took in a poll (`--timing`).
struct DriverTiming {
    std::string name;
    std::string lane;  // empty: the main chain
    unsigned ms = 0;
    int found = 0;
    bool skipped = false;  // backed off: slow and finding nothing in recent polls
};

struct PollOptions {
    bool waitForInitialControllers = true;
    bool parallel = true;                         // lanes of driver_lanes.h in their own threads
    backoff::Schedule* schedule = nullptr;        // skips slow drivers that find nothing; null runs all
    unsigned long long intervalMs = 60 * 1000;    // poll interval, for the backoff
    // Devices found so far, called from the poll threads while the poll runs (at most every 150 ms).
    std::function<void(std::vector<DeviceStatus>)> partial;
    std::vector<DriverTiming>* timings = nullptr;
};

// Queries every supported device. Blocking (can take a second or two); call from a worker thread.
std::vector<DeviceStatus> PollAllDevices(const PollOptions& options = {});
std::vector<DeviceStatus> PollWlmouseDevices();
#else
std::vector<DeviceStatus> PollAllDevices();
#endif

// Prints every HID collection (for adding support for new devices).
std::wstring DumpHidCollections();
