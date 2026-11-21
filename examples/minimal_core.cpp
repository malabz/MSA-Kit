// A minimal replacement core. Right-padding demonstrates the row contract.
#include "msakit/core.hpp"

#include <algorithm>
#include <cstddef>

namespace msakit {

Alignment align(const std::vector<SequenceRecord>& input) {
    std::size_t width = 0;
    for (const auto& record : input) {
        width = std::max(width, record.sequence.size());
    }

    Alignment result;
    result.rows.reserve(input.size());
    for (const auto& record : input) {
        result.rows.push_back(record.sequence);
        result.rows.back().resize(width, '-');
    }
    return result;
}

}  // namespace msakit
