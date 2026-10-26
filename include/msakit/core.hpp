#pragma once

#include <span>
#include <string>
#include <vector>

namespace msakit {

// Input position is the record identity; headers need not be unique.
struct SequenceRecord {
    std::string header;
    std::string sequence;
};

struct Alignment {
    std::vector<std::string> rows;
};

// Replace the implementation in src/core/star.cpp, preserving this contract.
// Input is nonempty, cleaned, gap-free and read-only. rows[i] belongs to input[i].
Alignment align(std::span<const SequenceRecord> input);

}  // namespace msakit
