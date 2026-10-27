#include "internal.hpp"
#include "msakit/core.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace msakit {
namespace {
using Score = std::int64_t;

bool is_residue(char value) {
    return value == 'A' || value == 'C' || value == 'G' || value == 'T' ||
           value == 'U' || value == 'N';
}

Score substitution(char center, char query) {
    if (center == 'N' || query == 'N') return 0;
    if (center == query || (center == 'T' && query == 'U') ||
        (center == 'U' && query == 'T')) return 1;
    return -1;
}

struct Projection {
    std::string at_center;
    std::vector<std::string> insertions;
};

Projection align_pair(std::string_view center, std::string_view query) {
    detail::check_score_bounds(center.size(), query.size());
    const auto height = detail::checked_add(center.size(), 1);
    const auto width = detail::checked_add(query.size(), 1);
    const auto cells = detail::checked_multiply(height, width);
    (void)detail::checked_multiply(cells, sizeof(Score));
    if (cells > std::vector<Score>().max_size()) {
        throw detail::Error(detail::kCoreError, "pairwise matrix exceeds container size");
    }

    // Deliberately use the full, easy-to-read DP table in this reference core.
    // It is local to this pair and is released before processing the next query.
    std::vector<Score> scores(cells);
    const auto cell = [&](std::size_t i, std::size_t j) -> Score& {
        return scores[i * width + j];
    };
    for (std::size_t i = 1; i < height; ++i) cell(i, 0) = -static_cast<Score>(i);
    for (std::size_t j = 1; j < width; ++j) cell(0, j) = -static_cast<Score>(j);
    for (std::size_t i = 1; i < height; ++i) {
        for (std::size_t j = 1; j < width; ++j) {
            cell(i, j) = std::max({
                cell(i - 1, j - 1) + substitution(center[i - 1], query[j - 1]),
                cell(i - 1, j) - 1,
                cell(i, j - 1) - 1});
        }
    }

    std::string aligned_center;
    std::string aligned_query;
    const auto longest_trace = detail::checked_add(center.size(), query.size());
    if (longest_trace > aligned_center.max_size()) {
        throw detail::Error(detail::kCoreError, "pairwise traceback exceeds string size");
    }
    aligned_center.reserve(longest_trace);
    aligned_query.reserve(longest_trace);
    std::size_t i = center.size();
    std::size_t j = query.size();
    while (i != 0 || j != 0) {
        // The order of these branches defines the documented tie rule:
        // consume both, consume center only, then consume query only.
        if (i != 0 && j != 0 && cell(i, j) ==
                cell(i - 1, j - 1) + substitution(center[i - 1], query[j - 1])) {
            aligned_center.push_back(center[--i]);
            aligned_query.push_back(query[--j]);
        } else if (i != 0 && cell(i, j) == cell(i - 1, j) - 1) {
            aligned_center.push_back(center[--i]);
            aligned_query.push_back('-');
        } else if (j != 0 && cell(i, j) == cell(i, j - 1) - 1) {
            aligned_center.push_back('-');
            aligned_query.push_back(query[--j]);
        } else {
            throw detail::Error(detail::kCoreError, "invalid pairwise traceback state");
        }
    }
    std::reverse(aligned_center.begin(), aligned_center.end());
    std::reverse(aligned_query.begin(), aligned_query.end());

    Projection projection;
    projection.at_center.assign(center.size(), '-');
    projection.insertions.resize(height);
    std::size_t consumed = 0;
    for (std::size_t column = 0; column < aligned_center.size(); ++column) {
        if (aligned_center[column] == '-') {
            // Slot s follows exactly s consumed center residues. This also
            // accounts for prefix (0) and suffix (center length) insertions.
            projection.insertions[consumed].push_back(aligned_query[column]);
        } else {
            projection.at_center[consumed++] = aligned_query[column];
        }
    }
    return projection;
}
}  // namespace

// Reference star alignment — replaceable core
// Replace this implementation to use another algorithm. Keep input unchanged,
// preserve row-to-input correspondence, and return only aligned strings.
Alignment align(const std::vector<SequenceRecord>& input) {
    if (input.empty()) {
        throw detail::Error(detail::kCoreError, "the alignment core requires at least one record");
    }
    for (const auto& record : input) {
        if (record.sequence.empty() ||
            !std::all_of(record.sequence.begin(), record.sequence.end(), is_residue)) {
            throw detail::Error(detail::kCoreError,
                                "the alignment core requires nonempty cleaned ACGTUN sequences");
        }
    }
    if (input.size() == 1) return Alignment{{input.front().sequence}};

    const auto& center = input.front().sequence;
    const auto slot_count = detail::checked_add(center.size(), 1);
    std::vector<std::size_t> slot_widths(slot_count, 0);
    std::vector<Projection> projections;
    projections.reserve(input.size() - 1);
    for (std::size_t row = 1; row < input.size(); ++row) {
        auto projection = align_pair(center, input[row].sequence);
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            slot_widths[slot] = std::max(slot_widths[slot], projection.insertions[slot].size());
        }
        projections.push_back(std::move(projection));
    }

    auto output_width = center.size();
    for (const auto count : slot_widths) output_width = detail::checked_add(output_width, count);
    (void)detail::checked_multiply(input.size(), output_width);
    if (output_width > std::string().max_size()) {
        throw detail::Error(detail::kCoreError, "alignment row exceeds string size");
    }

    Alignment result;
    result.rows.resize(input.size());
    for (std::size_t row = 0; row < input.size(); ++row) {
        auto& output = result.rows[row];
        output.reserve(output_width);
        const Projection* projection = row == 0 ? nullptr : &projections[row - 1];
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            std::size_t used = 0;
            if (projection != nullptr) {
                const auto& insertion = projection->insertions[slot];
                output.append(insertion);
                used = insertion.size();
            }
            // Each insertion string is left-aligned within its slot. A longer
            // query determines the width; shorter queries receive right gaps.
            output.append(slot_widths[slot] - used, '-');
            if (slot != center.size()) {
                output.push_back(projection == nullptr ? center[slot] : projection->at_center[slot]);
            }
        }
    }
    return result;
}
}  // namespace msakit
