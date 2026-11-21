#pragma once
#include <algorithm>
#include <utility>
#include <vector>

using ControllerProduct = std::pair<unsigned short, unsigned short>;

// Windows exposes model IDs on both APIs. Keep fallbacks when several physical devices
// could match, or one of the sources did not return a complete identity.
inline bool HasUniqueControllerMatch(ControllerProduct product, const std::vector<ControllerProduct>& from,
                                     const std::vector<ControllerProduct>& to) {
    return product.first && product.second &&
           std::count(from.begin(), from.end(), product) == 1 &&
           std::count(to.begin(), to.end(), product) == 1;
}
