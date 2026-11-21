#pragma once
// Lanes of the parallel poll (docs/adr/0007-parallel-poll-lanes.md). A lane is a group of drivers
// that run in their own thread while the main chain runs. It sees only the HID collections of its
// own vendor ids, and the main chain does not see them at all, so no driver of another vendor can
// send a request to a lane's device. Lanes without vendor ids are Bluetooth / file sources that use
// no HID collections. Results are merged in chain order before the generic fallbacks run.
//
// A vendor id belongs to a lane only when no driver outside it mentions that id:
// tests/driver_lanes_test.cpp checks this against the sources.

namespace lanes {

struct Lane {
    const char* name;
    unsigned short vid;        // 0: no HID collections (Bluetooth or file source)
    const char* drivers[3];    // driver names from devices.cpp in chain order; unused slots are null
    const char* files[3];      // their sources in src/drivers
};

inline constexpr Lane kLanes[] = {
    { "logitech",
      0x046D,
      { "LogitechG435", "Astro", "Logitech" },
      { "logitech_g435_0adf.cpp", "astro.cpp", "logitech.cpp" } },
    { "razer",
      0x1532,
      { "RazerHeadsets", "BarracudaPro", "Razer" },
      { "razer_headset.cpp", "barracuda_pro.cpp", "razer.cpp" } },
    { "steelseries", 0x1038, { "SteelSeriesElite", "SteelSeries" }, { "steelseries_elite.cpp", "steelseries.cpp" } },
    { "corsair", 0x1B1C, { "Corsair" }, { "corsair.cpp" } },
    { "jabra", 0x0B0E, { "Jabra" }, { "jabra.cpp" } },
    { "sony", 0x054C, { "SonyInzone", "PlayStation" }, { "sony_inzone.cpp", "playstation.cpp" } },
    { "rog", 0x0B05, { "Rog" }, { "rog.cpp" } },
    { "keychron", 0x3434, { "Keychron" }, { "keychron.cpp" } },
    { "rapoo", 0x24AE, { "Rapoo" }, { "rapoo.cpp" } },
    { "plantronics", 0x047F, { "Plantronics" }, { "plantronics.cpp" } },
    { "zowie", 0x04A5, { "Zowie" }, { "zowie.cpp" } },
    { "airpods", 0, { "AirPods" }, { "airpods.cpp" } },
    { "phonelink", 0, { "PhoneLink" }, { "phonelink.cpp" } },
    { "galaxybuds", 0, { "GalaxyBuds" }, { "galaxy_buds.cpp" } },
    { "nothingear", 0, { "NothingEar" }, { "nothing_ear.cpp" } },
};

}  // namespace lanes
