#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace msakit::detail {

// Checked arithmetic for the replaceable core.
inline std::size_t checked_add(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::length_error("alignment size addition overflow");
    }
    return left + right;
}

inline std::size_t checked_multiply(std::size_t left, std::size_t right) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::length_error("alignment size multiplication overflow");
    }
    return left * right;
}

// Bound every intermediate score by MaxStep * (center + query).
template<std::int64_t MaxStep>
inline void check_score_bounds(std::size_t center, std::size_t query) {
    static_assert(MaxStep > 0, "score bound must be positive");
    constexpr auto limit = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max() / MaxStep);
    if (center > limit || query > limit ||
        static_cast<std::uint64_t>(center) >
            limit - static_cast<std::uint64_t>(query)) {
        throw std::length_error("sequence lengths exceed signed score range");
    }
}

}  // namespace msakit::detail
