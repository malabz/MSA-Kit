#pragma once

#include "detail/app.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace msakit::detail {
struct CleaningStats {
    std::size_t uppercase_conversions = 0;
    std::size_t unknown_replacements = 0;
};

struct FileIdentity {
    std::uintmax_t device;
    std::uintmax_t inode;
};

struct InputData {
    std::vector<SequenceRecord> records;
    CleaningStats stats;
    std::optional<FileIdentity> identity;
};

InputData read_input(const std::string& path);
void check_output_path(const Options& options,
                       const std::optional<FileIdentity>& input_identity);
void write_output(const Options& options,
                  std::span<const SequenceRecord> input,
                  const Alignment& alignment,
                  const std::optional<FileIdentity>& input_identity);
}  // namespace msakit::detail
