#pragma once

#include "detail/errors.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace msakit::detail {

inline std::size_t checked_add(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw Error(kCoreError, "alignment size addition overflow");
    }
    return left + right;
}

inline std::size_t checked_multiply(std::size_t left, std::size_t right) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw Error(kCoreError, "alignment size multiplication overflow");
    }
    return left * right;
}

// Every global-alignment score lies between -(center + query) and
// min(center, query). Check before converting lengths to signed scores.
inline void check_score_bounds(std::size_t center, std::size_t query) {
    constexpr auto limit = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if (center > limit || query > limit ||
        static_cast<std::uint64_t>(center) >
            limit - static_cast<std::uint64_t>(query)) {
        throw Error(kCoreError, "sequence lengths exceed signed score range");
    }
}

}  // namespace msakit::detail
