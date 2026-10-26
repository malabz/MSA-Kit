#pragma once

#include "msakit/core.hpp"
#include <cstddef>
#include <string>

namespace msakit::detail {
struct Options {
    std::string input;
    std::string output;
    std::size_t line_width = 80;
    bool force = false;
    bool quiet = false;
};

using CoreFunction = Alignment (*)(std::span<const SequenceRecord>);

// The function parameter permits separate test executables to exercise errors
// from an alternate core without adding production CLI switches or backdoors.
int run_application(int argc, char** argv, CoreFunction core);
}  // namespace msakit::detail
