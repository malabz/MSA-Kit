#pragma once

// Internal operation boundary used by deterministic fault-injection tests.
// This is not a public extension point and is not exposed through the CLI.
#include "detail/io.hpp"
#include <filesystem>
#include <sys/types.h>
#include <zlib.h>

namespace msakit::detail {
class InputOperations {
public:
    virtual ~InputOperations() = default;
    virtual ssize_t read_fd(int fd, void* data, std::size_t size) noexcept;
    // Linux releases the descriptor even when close reports an error. Test
    // substitutes must preserve that ownership rule rather than leak it.
    virtual int close_fd(int fd) noexcept;
};

InputData read_input_with_operations(const std::string& path,
                                    InputOperations& operations);

class FileOperations {
public:
    virtual ~FileOperations() = default;
    virtual ssize_t write_fd(int fd, const void* data, std::size_t size) noexcept;
    virtual int close_fd(int fd) noexcept;
    virtual int deflate_step(z_stream* stream, int flush) noexcept;
    // Success transfers ownership of the completed output. The caller must
    // relinquish the temporary pathname, even if it has since been reused.
    // The link-based fallback performs its own best-effort temporary cleanup.
    virtual int publish(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        bool replace) noexcept;
};

void write_output_with_operations(
    const Options& options, std::span<const SequenceRecord> input,
    const Alignment& alignment,
    const std::optional<FileIdentity>& input_identity,
    FileOperations& operations);
}  // namespace msakit::detail
