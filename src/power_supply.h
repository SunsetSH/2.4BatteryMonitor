#pragma once
// Linux kernel batteries of peripherals (/sys/class/power_supply, scope "Device"): the kernel already
// reads them for hid-logitech-hidpp, hid-sony, hid-nintendo, hid-steam, hid-apple, Wacom and any HID
// device with a Battery Strength usage. The Linux build lists those its own drivers did not claim.
// Attribute names and values: Documentation/ABI/testing/sysfs-class-power in the kernel sources.
// Portable (std::filesystem only), so tests/power_supply_test.cpp runs it on a fake sysfs tree.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "drivers/drivers.h"

namespace power_supply {

struct Supply {
    std::string name;                          // directory name, e.g. "hid-6c:fb:ed:12:98:47-battery"
    std::map<std::string, std::string> attrs;  // type, scope, capacity, capacity_level, status, model_name, ...
    std::string device;                        // file name the "device" link points to, e.g. "0005:046D:B023.0003"
};

// "BBBB:VVVV:PPPP.NNNN" (a HID device: bus, vendor, product, instance) -> bus, vendor and product.
inline bool ParseHidId(const std::string& s, unsigned& bus, unsigned short& vid, unsigned short& pid) {
    unsigned b = 0, v = 0, p = 0, n = 0;
    char dot = 0;
    if (s.size() < 19 || sscanf(s.c_str(), "%4x:%4x:%4x%c%4x", &b, &v, &p, &dot, &n) != 5 || dot != '.') return false;
    bus = b;
    vid = (unsigned short)v;
    pid = (unsigned short)p;
    return true;
}

inline std::string Attr(const Supply& s, const char* name) {
    auto it = s.attrs.find(name);
    return it == s.attrs.end() ? "" : it->second;
}

// A peripheral's battery as a device, or false for system batteries, mains adapters, UPS and
// supplies without a usable level.
inline bool ToStatus(const Supply& s, DeviceStatus& out) {
    if (Attr(s, "scope") != "Device") return false;
    const std::string type = Attr(s, "type");
    if (!type.empty() && type != "Battery") return false;
    DeviceStatus d;
    const std::string capacity = Attr(s, "capacity"), level = Attr(s, "capacity_level"), status = Attr(s, "status");
    if (!capacity.empty()) {
        char* end = nullptr;
        long v = strtol(capacity.c_str(), &end, 10);
        if (end != capacity.c_str() && *end == 0 && v >= 0 && v <= 100) d.percent = (int)v;
    }
    if (d.percent < 0) {  // coarse levels only (some HID++ 1.0 devices, Joy-Con)
        static const std::pair<const char*, int> kLevels[] = {
            { "Full", 100 }, { "High", 75 }, { "Normal", 50 }, { "Low", 20 }, { "Critical", 5 },
        };
        for (const auto& [word, pct] : kLevels)
            if (level == word) {
                d.percent = pct;
                d.levelText = level == "High"     ? L"Good"
                              : level == "Normal" ? L"Medium"
                                                  : std::wstring(level.begin(), level.end());
            }
    }
    if (d.percent < 0) return false;
    d.charging = status == "Charging";
    d.full = status == "Full";
    if (d.full) d.charging = true;  // on power and full, like the vendor drivers report it
    std::string model = Attr(s, "model_name"), maker = Attr(s, "manufacturer");
    std::string name =
        model.empty() ? s.name : (!maker.empty() && model.rfind(maker, 0) != 0 ? maker + " " + model : model);
    d.name = Utf8ToWide(name);
    unsigned bus = 0;
    if (ParseHidId(s.device, bus, d.vid, d.pid))
        d.via = bus == 0x0005 ? L"Bluetooth" : L"2.4 GHz receiver / USB";
    else
        d.via = L"USB / Bluetooth";
    const std::string serial = Attr(s, "serial_number");
    // The directory name of a HID++ battery counts up on every reconnect; model and serial do not.
    d.key = Utf8ToWide("psu:" + (serial.empty() ? s.name : model + ":" + serial));
    d.kind = GuessKind(d.name);
    out = d;
    return true;
}

inline std::string ReadLine(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\n')) line.pop_back();
    return line;
}

// Reads every supply under `root` (/sys/class/power_supply on Linux).
inline std::vector<Supply> ReadSupplies(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<Supply> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        Supply s;
        s.name = e.path().filename().string();
        for (const char* a : { "type", "scope", "capacity", "capacity_level", "status", "model_name", "manufacturer",
                               "serial_number" }) {
            const fs::path p = e.path() / a;
            if (fs::is_regular_file(p, ec)) s.attrs[a] = ReadLine(p);
        }
        std::error_code linkError;
        const fs::path target = fs::read_symlink(e.path() / "device", linkError);
        if (!linkError) s.device = target.filename().string();
        out.push_back(std::move(s));
    }
    return out;
}

// Adds the kernel batteries of devices no driver claimed; call after the vendor drivers.
inline void Poll(PollContext& ctx, const std::filesystem::path& root) {
    for (const Supply& s : ReadSupplies(root)) {
        DeviceStatus d;
        if (!ToStatus(s, d)) continue;
        if (d.vid && ctx.IsClaimed(d.vid, d.pid)) continue;
        bool listed = false;
        for (const auto& o : ctx.out) listed |= o.key == d.key || (!d.vid && o.name == d.name);
        if (listed) continue;
        ctx.Add(d, d.vid, d.pid);
    }
}

}  // namespace power_supply
