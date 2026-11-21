#include "../src/update_core.h"
#include <cassert>

int main() {
    using namespace updates;
    assert(ParseVersion(L"1.10.0") > ParseVersion(L"1.9.9"));
    assert(!ParseVersion(L"1.0.0-beta"));
    assert(!ParseVersion(L"1.01.0"));
    assert(!ParseVersion(L"1.65536.0"));
    constexpr auto repo = L"SunsetSH/2.4BatteryMonitor";
    assert(ReleaseTag(L"https://github.com/SunsetSH/2.4BatteryMonitor/releases/tag/v1.3.0", repo) == L"1.3.0");
    assert(ReleaseTag(L"/SunsetSH/2.4BatteryMonitor/releases/tag/v1.3.0", repo) == L"1.3.0");
    assert(ReleaseTag(L"https://github.com.evil.test/SunsetSH/2.4BatteryMonitor/releases/tag/v1.3.0", repo).empty());
    assert(ReleaseTag(L"https://github.com/Other/2.4BatteryMonitor/releases/tag/v1.3.0", repo).empty());
    assert(ReleaseTag(L"/SunsetSH/2.4BatteryMonitor/releases/tag/v1.3.0?x=1", repo).empty());
    assert(ReleaseAssetHost(L"release-assets.githubusercontent.com"));
    assert(!ReleaseAssetHost(L"release-assets.githubusercontent.com.evil.test"));
    assert(SignedPayload("owner/repo", "setup.exe", "abc") == "2.4BatteryMonitor release v1\nowner/repo\nsetup.exe\nabc\n");
}
