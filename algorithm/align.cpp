// Replace this file to supply your own msakit::align(). The public interface
// is in msakit/core.hpp. See examples/minimal_core.cpp for a complete example.
#include "msakit/core.hpp"
#include "msakit/detail/checked_size.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace msakit {
namespace {
using Score = std::int64_t;
constexpr Score kPairGapCost = 3;

bool is_residue(char value) {
    return value == 'A' || value == 'C' || value == 'G' || value == 'T' ||
           value == 'U' || value == 'N';
}

constexpr std::int8_t substitution(char center, char query) {
    if (center == 'N' || query == 'N') return 0;
    if (center == query || (center == 'T' && query == 'U') ||
        (center == 'U' && query == 'T')) return 1;
    return -1;
}

// The inner DP loop uses one table row for each center residue. Both inputs
// have already been checked against the uppercase ACGTUN alphabet.
constexpr auto kSubstitutions = [] {
    std::array<std::array<std::int8_t, 26>, 26> scores{};
    constexpr std::string_view alphabet = "ACGTUN";
    for (const char a : alphabet) {
        for (const char b : alphabet) scores[a - 'A'][b - 'A'] = substitution(a, b);
    }
    return scores;
}();

struct Projection {
    std::string at_center;
    std::vector<std::string> insertions;
};

Projection align_pair(std::string_view center, std::string_view query) {
    detail::check_score_bounds<kPairGapCost>(center.size(), query.size());
    const auto height = detail::checked_add(center.size(), 1);
    const auto width = detail::checked_add(query.size(), 1);
    const auto cells = detail::checked_multiply(height, width);
    (void)detail::checked_multiply(cells, sizeof(Score));
    if (cells > std::vector<Score>().max_size()) {
        throw std::length_error("pairwise matrix exceeds container size");
    }

    // Deliberately use the full, easy-to-read DP table in this reference core.
    // It is local to this pair and is released before processing the next query.
    std::vector<Score> scores(cells);
    const auto cell = [&](std::size_t i, std::size_t j) -> Score& {
        return scores[i * width + j];
    };
    for (std::size_t i = 1; i < height; ++i) cell(i, 0) = -kPairGapCost * static_cast<Score>(i);
    for (std::size_t j = 1; j < width; ++j) cell(0, j) = -kPairGapCost * static_cast<Score>(j);
    for (std::size_t i = 1; i < height; ++i) {
        const auto& substitutions = kSubstitutions[center[i - 1] - 'A'];
        for (std::size_t j = 1; j < width; ++j) {
            cell(i, j) = std::max({
                cell(i - 1, j - 1) + substitutions[query[j - 1] - 'A'],
                cell(i - 1, j) - kPairGapCost,
                cell(i, j - 1) - kPairGapCost});
        }
    }

    std::string aligned_center;
    std::string aligned_query;
    const auto longest_trace = detail::checked_add(center.size(), query.size());
    if (longest_trace > aligned_center.max_size()) {
        throw std::length_error("pairwise traceback exceeds string size");
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
        } else if (i != 0 && cell(i, j) == cell(i - 1, j) - kPairGapCost) {
            aligned_center.push_back(center[--i]);
            aligned_query.push_back('-');
        } else if (j != 0 && cell(i, j) == cell(i, j - 1) - kPairGapCost) {
            aligned_center.push_back('-');
            aligned_query.push_back(query[--j]);
        } else {
            throw std::logic_error("invalid pairwise traceback state");
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

constexpr std::size_t kRadius = 4;
constexpr std::size_t kBandSize = 2 * kRadius + 1;
constexpr Score kProfileGapCost = 2;
using Column = std::array<Score, 6>;  // A, C, G, T/U, N, gap
constexpr std::size_t kUnknown = 4, kGap = 5;

std::size_t symbol(char c) {
    switch (c) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': case 'U': return 3;
        case 'N': return kUnknown;
        default: return kGap;  // rows have already been built from cleaned input
    }
}

void count_row(std::vector<Column>& profile, const std::string& row, int change) {
    for (std::size_t c = 0; c < row.size(); ++c) {
        profile[c][symbol(row[c])] += change;
    }
}

// Score gain from placing a base instead of a gap against the other rows.
// Dropping the position-independent term kProfileGapCost * other_rows gives
// the same best path. N contributes zero to all base/base comparisons.
Score placement(const Column& counts, std::size_t base, Score other_rows) {
    const auto gaps = counts[kGap];
    const auto known = other_rows - gaps - counts[kUnknown];
    const auto substitutions = base == kUnknown ? 0 : 2 * counts[base] - known;
    return substitutions - 2 * kProfileGapCost * gaps;
}

std::size_t band_start(std::size_t residue, std::size_t original) {
    return original - std::min(kRadius, original - residue);
}

// Choose a strictly increasing column for each residue within its original
// radius. Prefix maxima make each DP layer linear in the band width.
void refine_row(std::string& row, const std::string& sequence,
                const std::vector<Column>& profile, Score other_rows) {
    std::vector<std::size_t> original;
    original.reserve(sequence.size());
    for (std::size_t c = 0; c < row.size(); ++c) {
        if (row[c] != '-') original.push_back(c);
    }
    // Compare scores first and then prefer the least total movement.
    using Path = std::pair<Score, Score>;
    std::array<Path, kBandSize> previous{}, current{};
    std::vector<std::array<std::uint8_t, kBandSize>> parent(sequence.size());
    std::size_t previous_start = 0, previous_count = 0;
    Score original_score = 0;
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        const auto old = original[i];
        const auto base = symbol(sequence[i]);
        original_score += placement(profile[old], base, other_rows);
        // Leave enough columns for all earlier and later residues.
        const auto start = band_start(i, old);
        const auto last = old + std::min(kRadius, (row.size() - sequence.size() + i) - old);
        std::size_t best = 0, next = 1;
        for (std::size_t c = start; c <= last; ++c) {
            while (next < previous_count && previous_start + next < c) {
                if (previous[next] > previous[best]) best = next;
                ++next;
            }
            const auto slot = c - start;
            auto path = i == 0 ? Path{} : previous[best];
            path.first += placement(profile[c], base, other_rows);
            path.second -= static_cast<Score>(c > old ? c - old : old - c);
            current[slot] = path;
            parent[i][slot] = static_cast<std::uint8_t>(best);
        }
        previous = current;
        previous_start = start;
        previous_count = last - start + 1;
    }
    const auto best = std::max_element(previous.begin(), previous.begin() + previous_count);
    if (best->first <= original_score) return;
    std::size_t slot = static_cast<std::size_t>(best - previous.begin());
    std::fill(row.begin(), row.end(), '-');
    for (std::size_t i = sequence.size(); i-- > 0;) {
        row[band_start(i, original[i]) + slot] = sequence[i];
        slot = parent[i][slot];
    }
}

void refine_alignment(Alignment& alignment, const std::vector<SequenceRecord>& input) {
    auto& rows = alignment.rows;
    const auto width = rows.front().size();
    if (rows.size() < 3 || std::all_of(input.begin(), input.end(),
            [width](const auto& record) { return record.sequence.size() == width; })) {
        return;
    }

    // Bound both accumulated placement scores and total movement before DP.
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<Score>::max());
    if (rows.size() > limit / (2 * kProfileGapCost) / width || width > limit / kRadius) {
        throw std::length_error("profile alignment exceeds signed score range");
    }
    const auto count = static_cast<Score>(rows.size());
    std::vector<Column> profile(width, Column{});
    for (const auto& row : rows) count_row(profile, row, 1);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (input[i].sequence.size() == width) continue;
        count_row(profile, rows[i], -1);
        refine_row(rows[i], input[i].sequence, profile, count - 1);
        count_row(profile, rows[i], 1);
    }
    std::size_t kept = 0;
    for (std::size_t c = 0; c < width; ++c) {
        if (profile[c][kGap] == count) continue;
        if (kept != c) {
            for (auto& row : rows) row[kept] = row[c];
        }
        ++kept;
    }
    for (auto& row : rows) row.resize(kept);
}

Alignment build_star(const std::vector<SequenceRecord>& input) {
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
        throw std::length_error("alignment row exceeds string size");
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
}  // namespace

// Reference star alignment — replaceable core
// Replace this implementation to use another algorithm. Keep input unchanged,
// preserve row-to-input correspondence, and return only aligned strings.
Alignment align(const std::vector<SequenceRecord>& input) {
    if (input.empty()) {
        throw std::invalid_argument("the alignment core requires at least one record");
    }
    for (const auto& record : input) {
        if (record.sequence.empty() ||
            !std::all_of(record.sequence.begin(), record.sequence.end(), is_residue)) {
            throw std::invalid_argument("the alignment core requires nonempty cleaned ACGTUN sequences");
        }
    }
    if (input.size() == 1) return Alignment{{input.front().sequence}};
    auto result = build_star(input);
    refine_alignment(result, input);
    return result;
}
}  // namespace msakit
