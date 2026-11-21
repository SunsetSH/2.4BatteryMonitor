// Checks the parallel lanes of src/driver_lanes.h against the sources (run from the repository root):
//  * a lane's vendor id appears (as 0xNNNN) in no driver outside the lane, so no other driver would
//    have looked at those devices in the sequential chain;
//  * every lane driver is in the chain of src/devices.cpp, and its source file exists.
// The generic fallbacks run after the lanes are merged, so they may mention any vendor id.
#include "../src/driver_lanes.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    std::string text = s.str();
    for (auto& ch : text) ch = (char)toupper((unsigned char)ch);
    return text;
}

// Known mentions of a lane's id that are not that vendor id: Jabra lists 0x1038 as a product id.
bool Allowed(const std::string& file, const char* hex) { return file == "jabra.cpp" && !strcmp(hex, "1038"); }

}  // namespace

int main() {
    int failures = 0;
    const std::string chain = Read("src/devices.cpp");
    for (const auto& lane : lanes::kLanes) {
        for (const char* driver : lane.drivers) {
            if (!driver) continue;
            std::string entry = std::string("{ \"") + driver + "\", POLL";
            for (auto& ch : entry) ch = (char)toupper((unsigned char)ch);
            if (chain.find(entry) == std::string::npos) {
                std::printf("FAIL: lane %s: driver %s is not in the chain of src/devices.cpp\n", lane.name, driver);
                ++failures;
            }
        }
        for (const char* file : lane.files)
            if (file && !fs::exists(fs::path("src/drivers") / file)) {
                std::printf("FAIL: lane %s: src/drivers/%s does not exist\n", lane.name, file);
                ++failures;
            }
        if (!lane.vid) continue;
        char hex[8];
        snprintf(hex, sizeof hex, "%04X", lane.vid);
        const std::string needle = std::string("0X") + hex;
        bool inLane = false;
        for (const auto& e : fs::directory_iterator("src/drivers")) {
            const std::string name = e.path().filename().string();
            if (e.path().extension() != ".cpp" && e.path().extension() != ".h") continue;
            bool own = false;
            for (const char* f : lane.files) own |= f && name == f;
            const bool mentions = Read(e.path()).find(needle) != std::string::npos;
            if (own)
                inLane |= mentions;
            else if (mentions && name != "generic.cpp" && name != "xinput.cpp" && name != "drivers.h" &&
                     !Allowed(name, hex)) {
                std::printf("FAIL: lane %s: %s also mentions %s; that driver must join the lane\n", lane.name,
                            name.c_str(), needle.c_str());
                ++failures;
            }
        }
        if (!inLane) {
            std::printf("FAIL: lane %s: none of its sources mentions %s\n", lane.name, needle.c_str());
            ++failures;
        }
    }
    if (failures) return 1;
    std::printf("driver_lanes_test passed (%zu lanes)\n", sizeof(lanes::kLanes) / sizeof(lanes::kLanes[0]));
    return 0;
}
