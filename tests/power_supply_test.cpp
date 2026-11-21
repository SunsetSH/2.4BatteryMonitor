// Kernel peripheral batteries (src/power_supply.h) on a fake /sys/class/power_supply tree in a
// temporary directory. The "device" symlink is not created (Windows needs extra rights for it);
// ParseHidId covers what it points to.
#include "../src/power_supply.h"

#include <cassert>
#include <cstdio>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path& dir, const char* name, const std::string& value) {
    fs::create_directories(dir);
    std::ofstream(dir / name) << value << "\n";
}

}  // namespace

int main() {
    unsigned bus = 0;
    unsigned short vid = 0, pid = 0;
    assert(power_supply::ParseHidId("0005:046D:B023.0003", bus, vid, pid) && bus == 5 && vid == 0x046D &&
           pid == 0xB023);
    assert(power_supply::ParseHidId("0003:054C:0CE6.000A", bus, vid, pid) && bus == 3 && pid == 0x0CE6);
    assert(!power_supply::ParseHidId("input12", bus, vid, pid));
    assert(!power_supply::ParseHidId("0005:046D:B023", bus, vid, pid));

    const fs::path root = fs::temp_directory_path() / "24bm-power-supply-test";
    fs::remove_all(root);
    // A Bluetooth mouse through hid-input (capacity and status), with model and maker.
    Write(root / "hid-mouse-battery", "type", "Battery");
    Write(root / "hid-mouse-battery", "scope", "Device");
    Write(root / "hid-mouse-battery", "capacity", "64");
    Write(root / "hid-mouse-battery", "status", "Discharging");
    Write(root / "hid-mouse-battery", "model_name", "MX Master 3S");
    Write(root / "hid-mouse-battery", "manufacturer", "Logitech");
    // A HID++ device with a coarse level only, charging, and a serial (its key survives reconnects).
    Write(root / "hidpp_battery_3", "type", "Battery");
    Write(root / "hidpp_battery_3", "scope", "Device");
    Write(root / "hidpp_battery_3", "capacity_level", "High");
    Write(root / "hidpp_battery_3", "status", "Charging");
    Write(root / "hidpp_battery_3", "model_name", "K380 Keyboard");
    Write(root / "hidpp_battery_3", "serial_number", "4a2b9c1d");
    // A controller at full on its cable.
    Write(root / "sony_controller_battery_aa", "type", "Battery");
    Write(root / "sony_controller_battery_aa", "scope", "Device");
    Write(root / "sony_controller_battery_aa", "capacity", "100");
    Write(root / "sony_controller_battery_aa", "status", "Full");
    Write(root / "sony_controller_battery_aa", "model_name", "Wireless Controller");
    // Not peripherals or not usable: laptop battery, mains, out of range, unknown level.
    Write(root / "BAT0", "type", "Battery");
    Write(root / "BAT0", "capacity", "80");
    Write(root / "AC", "type", "Mains");
    Write(root / "AC", "scope", "Device");
    Write(root / "hid-broken-battery", "scope", "Device");
    Write(root / "hid-broken-battery", "capacity", "180");
    Write(root / "hid-unknown-battery", "scope", "Device");
    Write(root / "hid-unknown-battery", "capacity_level", "Unknown");

    PollContext ctx;
    power_supply::Poll(ctx, root);
    assert(ctx.out.size() == 3);
    const DeviceStatus *mouse = nullptr, *kbd = nullptr, *pad = nullptr;
    for (const auto& d : ctx.out) {
        if (d.name == L"Logitech MX Master 3S") mouse = &d;
        if (d.name == L"K380 Keyboard") kbd = &d;
        if (d.name == L"Wireless Controller") pad = &d;
    }
    assert(mouse && kbd && pad);
    assert(mouse->percent == 64 && !mouse->charging && mouse->kind == DeviceKind::Other);
    assert(mouse->key == L"psu:hid-mouse-battery" && mouse->via == L"USB / Bluetooth");
    assert(kbd->percent == 75 && kbd->levelText == L"Good" && kbd->charging && kbd->kind == DeviceKind::Keyboard);
    assert(kbd->key == L"psu:K380 Keyboard:4a2b9c1d");
    assert(pad->percent == 100 && pad->full && pad->kind == DeviceKind::Gamepad);

    // A device a driver already listed is not added twice.
    PollContext again;
    DeviceStatus listed;
    listed.key = L"psu:hid-mouse-battery";
    again.out.push_back(listed);
    power_supply::Poll(again, root);
    assert(again.out.size() == 3);

    // A missing directory is no devices, not an error.
    PollContext none;
    power_supply::Poll(none, root / "missing");
    assert(none.out.empty());

    fs::remove_all(root);
    std::printf("power_supply_test passed\n");
    return 0;
}
