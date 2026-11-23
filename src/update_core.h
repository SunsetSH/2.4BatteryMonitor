#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace updates {

using Version = std::array<unsigned, 3>;

inline std::optional<Version> ParseVersion(std::wstring_view text) {
    Version parts{};
    for (size_t i = 0; i < parts.size(); ++i) {
        auto dot = text.find(L'.');
        if ((dot != text.npos) != (i + 1 < parts.size())) return {};
        auto part = text.substr(0, dot);
        if (part.empty() || part.size() > 5 || (part.size() > 1 && part.front() == L'0')) return {};
        for (wchar_t c : part) {
            if (c < L'0' || c > L'9') return {};
            parts[i] = parts[i] * 10 + (c - L'0');
        }
        if (parts[i] > 65535) return {};
        if (dot != text.npos) text.remove_prefix(dot + 1);
    }
    return parts;
}

inline std::wstring ReleaseTag(std::wstring_view location, std::wstring_view repo) {
    constexpr std::wstring_view host = L"https://github.com";
    if (location.substr(0, host.size()) == host) location.remove_prefix(host.size());
    const auto prefix = L"/" + std::wstring(repo) + L"/releases/tag/v";
    if (location.substr(0, prefix.size()) != prefix) return {};
    auto version = location.substr(prefix.size());
    return ParseVersion(version) ? std::wstring(version) : L"";
}

inline bool ReleaseAssetHost(std::wstring_view host) {
    return host == L"release-assets.githubusercontent.com" || host == L"objects.githubusercontent.com";
}

inline std::string SignedPayload(std::string_view repo, std::string_view name, std::string_view hash) {
    return "2.4BatteryMonitor release v1\n" + std::string(repo) + "\n" + std::string(name) + "\n" + std::string(hash) + "\n";
}

// Release assets an installed copy (the Inno Setup installer) and a portable copy (the bare executable)
// update from. The version is part of the name, and the name is part of the signed payload, so an older
// signed file cannot be offered as a newer one.
inline std::wstring SetupAssetName(std::wstring_view version) {
    return L"2.4BatteryMonitor-Setup-" + std::wstring(version) + L".exe";
}
inline std::wstring PortableAssetName(std::wstring_view version) {
    return L"2.4BatteryMonitor-" + std::wstring(version) + L".exe";
}
}
