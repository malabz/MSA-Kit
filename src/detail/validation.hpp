#pragma once

#include "msakit/core.hpp"

namespace msakit::detail {
void validate_alignment(std::span<const SequenceRecord> input,
                        const Alignment& alignment);
}  // namespace msakit::detail
