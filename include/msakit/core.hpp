#pragma once

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

// Implement this function in algorithm/align.cpp. Input records are nonempty,
// cleaned (ACGTUN), gap-free and in input order. Headers can repeat.
// Return one equal-width row per record in the same order. Removing '-' from
// rows[i] must recover input[i].sequence. Every column must contain a residue.
// Throw an exception to report failure; the framework handles I/O and errors.
Alignment align(const std::vector<SequenceRecord>& input);

}  // namespace msakit
