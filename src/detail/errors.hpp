#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace msakit::detail {
inline constexpr int kCliError = 2;
inline constexpr int kInputError = 3;
inline constexpr int kIoError = 4;
inline constexpr int kCoreError = 5;

class Error : public std::runtime_error {
public:
    Error(int code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    int exit_code() const noexcept { return code_; }
private:
    int code_;
};
}  // namespace msakit::detail
