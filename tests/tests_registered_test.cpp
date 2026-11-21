// Every tests/*.cpp must be built by a Makefile (Makefile or linux/Makefile.linux); a forgotten test
// silently stops guarding its driver. Run from the repository root.
#include <cstdio>
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
    return s.str();
}

}  // namespace

int main() {
    const std::string makefiles = Read("Makefile") + Read("linux/Makefile.linux");
    if (makefiles.empty()) {
        std::printf("Makefile not found (run from the repository root)\n");
        return 1;
    }
    int failures = 0, count = 0;
    for (const auto& e : fs::directory_iterator("tests")) {
        if (e.path().extension() != ".cpp") continue;
        ++count;
        const std::string stem = e.path().stem().string();
        // win_test / portable_test register "name,"; the icon and Linux tests are built by their own rules.
        if (makefiles.find("test," + stem + ",") == std::string::npos &&
            makefiles.find("tests/" + stem + ".cpp") == std::string::npos) {
            std::printf("FAIL: tests/%s.cpp is not built by the Makefile\n", stem.c_str());
            ++failures;
        }
    }
    if (failures) return 1;
    std::printf("tests_registered_test passed (%d test sources)\n", count);
    return 0;
}
