#include "detail/io.hpp"
#include "detail/errors.hpp"
#include "detail/io_testing.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zlib.h>

namespace msakit::detail {
namespace {
constexpr std::size_t kBufferSize = 64 * 1024;

[[noreturn]] void fail_io(const std::string& action, const std::string& path,
                          int error = errno) {
    throw Error(kIoError, action + ": " + path + ": " + std::strerror(error));
}

[[noreturn]] void fail_zlib(const std::string& action, const std::string& path,
                           int result, const z_stream& stream) {
    if (result == Z_MEM_ERROR) {
        throw Error(kCoreError, "Memory allocation failed during " + action);
    }
    throw Error(kIoError, action + ": " + path +
                             (stream.msg ? ": " + std::string(stream.msg) : ""));
}

bool ascii_space(unsigned char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == '\v' || ch == '\f';
}

void increment(std::size_t& value) {
    if (value == std::numeric_limits<std::size_t>::max()) {
        throw Error(kCoreError, "Input statistics overflow");
    }
    ++value;
}

// Adapted from HAlign-5 include/utils.h at
// b0d5608811d44e30bc323da54f0583ceaaf684d2.
// Copyright (c) 2026 HAlign-5 contributors. MIT license; see LICENSE and
// the repository's third-party notices. Gaps are rejected before this table.
constexpr auto make_clean_table() {
    std::array<unsigned char, 256> table{};
    table.fill('N');
    constexpr std::string_view upper = "ACGTUN";
    constexpr std::string_view lower = "acgtun";
    for (std::size_t i = 0; i < upper.size(); ++i) {
        table[static_cast<unsigned char>(upper[i])] = upper[i];
        table[static_cast<unsigned char>(lower[i])] = upper[i];
    }
    return table;
}
constexpr auto kClean = make_clean_table();

class InputFd {
public:
    InputFd(const std::string& path, InputOperations& operations)
        : path_(path), owned_(path != "-"), operations_(operations) {
        fd_ = owned_ ? ::open(path.c_str(), O_RDONLY | O_CLOEXEC) : STDIN_FILENO;
        if (fd_ < 0) fail_io("Cannot open input", path_);
    }
    ~InputFd() { if (owned_ && fd_ >= 0) operations_.close_fd(fd_); }
    InputFd(const InputFd&) = delete;
    InputFd& operator=(const InputFd&) = delete;

    std::size_t read(unsigned char* data, std::size_t size) {
        ssize_t count;
        do { count = operations_.read_fd(fd_, data, size); } while (count < 0 && errno == EINTR);
        if (count < 0) fail_io("Cannot read input", path_);
        return static_cast<std::size_t>(count);
    }
    std::optional<FileIdentity> identity() const {
        if (!owned_) return std::nullopt;
        struct stat st {};
        if (::fstat(fd_, &st) != 0) fail_io("Cannot inspect input", path_);
        return FileIdentity{static_cast<std::uintmax_t>(st.st_dev),
                            static_cast<std::uintmax_t>(st.st_ino)};
    }
    void finish() {
        if (owned_ && fd_ >= 0) {
            const int old = std::exchange(fd_, -1);
            // Linux releases the descriptor even when close reports an error.
            // Retrying could close an unrelated descriptor that reused its number.
            if (operations_.close_fd(old) != 0) fail_io("Cannot close input", path_);
        }
    }
private:
    std::string path_;
    bool owned_;
    InputOperations& operations_;
    int fd_ = -1;
};

class DecodedInput {
public:
    DecodedInput(const std::string& path, InputOperations& operations)
        : file_(path, operations), path_(path) {
        identity_ = file_.identity();
        // Pipes may return only one byte, so accumulate the complete magic prefix.
        while (prefix_size_ < 2) {
            const auto n = file_.read(encoded_.data() + prefix_size_, 2 - prefix_size_);
            if (n == 0) break;
            prefix_size_ += n;
        }
        gzip_ = prefix_size_ == 2 && encoded_[0] == 0x1f && encoded_[1] == 0x8b;
        if (gzip_) {
            const int result = ::inflateInit2(&stream_, 16 + MAX_WBITS);
            if (result != Z_OK) fail_zlib("Cannot initialize gzip input", path_, result, stream_);
            initialized_ = true;
            stream_.next_in = encoded_.data();
            stream_.avail_in = static_cast<uInt>(prefix_size_);
        }
    }
    ~DecodedInput() { if (initialized_) ::inflateEnd(&stream_); }
    DecodedInput(const DecodedInput&) = delete;
    DecodedInput& operator=(const DecodedInput&) = delete;

    const std::optional<FileIdentity>& identity() const { return identity_; }

    bool line(std::string& result) {
        result.clear();
        while (true) {
            const int ch = get();
            if (ch < 0) return !result.empty();
            if (ch == '\n') {
                if (!result.empty() && result.back() == '\r') result.pop_back();
                return true;
            }
            result.push_back(static_cast<char>(ch));
        }
    }

    void finish() {
        if (initialized_) {
            const int result = ::inflateEnd(&stream_);
            initialized_ = false;
            if (result != Z_OK) fail_zlib("Cannot finalize gzip input", path_, result, stream_);
        }
        file_.finish();
    }
private:
    int get() {
        if (decoded_pos_ == decoded_size_) {
            decoded_size_ = fill();
            decoded_pos_ = 0;
            if (decoded_size_ == 0) return -1;
        }
        return decoded_[decoded_pos_++];
    }

    bool refill_compressed() {
        const auto size = file_.read(encoded_.data(), encoded_.size());
        stream_.next_in = encoded_.data();
        stream_.avail_in = static_cast<uInt>(size);
        return size != 0;
    }

    std::size_t fill() {
        if (!gzip_) {
            if (prefix_size_ != 0) {
                const auto n = std::exchange(prefix_size_, 0);
                std::copy_n(encoded_.begin(), n, decoded_.begin());
                return n;
            }
            return file_.read(decoded_.data(), decoded_.size());
        }
        if (finished_) return 0;
        stream_.next_out = decoded_.data();
        stream_.avail_out = static_cast<uInt>(decoded_.size());
        while (true) {
            if (member_done_) {
                if (stream_.avail_in == 0 && !refill_compressed()) {
                    finished_ = true;
                    return 0;
                }
                // gzip permits concatenated members. Reset without losing bytes
                // already read beyond the previous trailer; garbage fails parsing.
                auto* next = stream_.next_in;
                const auto available = stream_.avail_in;
                const int reset = ::inflateReset2(&stream_, 16 + MAX_WBITS);
                if (reset != Z_OK) fail_zlib("Cannot reset gzip input", path_, reset, stream_);
                stream_.next_in = next;
                stream_.avail_in = available;
                stream_.next_out = decoded_.data();
                stream_.avail_out = static_cast<uInt>(decoded_.size());
                member_done_ = false;
            }
            if (stream_.avail_in == 0 && !refill_compressed()) {
                throw Error(kIoError, "Truncated gzip input: " + path_);
            }
            const int result = ::inflate(&stream_, Z_NO_FLUSH);
            const auto produced = decoded_.size() - stream_.avail_out;
            if (result == Z_STREAM_END) member_done_ = true;
            else if (result != Z_OK && !(result == Z_BUF_ERROR && stream_.avail_in == 0)) {
                fail_zlib("Invalid gzip input", path_, result, stream_);
            }
            if (produced != 0) return produced;
        }
    }

    InputFd file_;
    std::string path_;
    std::optional<FileIdentity> identity_;
    std::array<unsigned char, kBufferSize> encoded_{};
    std::array<unsigned char, kBufferSize> decoded_{};
    std::size_t prefix_size_ = 0;
    std::size_t decoded_size_ = 0;
    std::size_t decoded_pos_ = 0;
    z_stream stream_{};
    bool gzip_ = false;
    bool initialized_ = false;
    bool member_done_ = false;
    bool finished_ = false;
};

class OutputTransaction {
public:
    OutputTransaction(const std::string& destination, bool force, FileOperations& operations)
        : destination_(destination), force_(force), operations_(operations) {
        auto parent = destination_.parent_path();
        if (parent.empty()) parent = ".";
        temporary_ = (parent / ".msakit-XXXXXX").string();
        fd_ = ::mkstemp(temporary_.data());
        if (fd_ < 0) fail_io("Cannot create temporary output", destination);
        if (::fcntl(fd_, F_SETFD, FD_CLOEXEC) == -1) {
            const int saved = errno;
            ::close(std::exchange(fd_, -1));
            ::unlink(temporary_.c_str());
            fail_io("Cannot configure temporary output", destination, saved);
        }
    }
    ~OutputTransaction() {
        if (fd_ >= 0) ::close(fd_);
        // Only an unpublished transaction still owns this name. Publication
        // releases it; another file may immediately reuse the old temporary name.
        if (!temporary_.empty()) ::unlink(temporary_.c_str());
    }
    OutputTransaction(const OutputTransaction&) = delete;
    OutputTransaction& operator=(const OutputTransaction&) = delete;
    int fd() const noexcept { return fd_; }
    void commit() {
        const int old = std::exchange(fd_, -1);
        if (operations_.close_fd(old) != 0) fail_io("Cannot close output", destination_.string());
        if (operations_.publish(temporary_, destination_, force_) != 0) {
            fail_io("Cannot publish output", destination_.string());
        }
        temporary_.clear();
    }
private:
    std::filesystem::path destination_;
    std::string temporary_;
    bool force_;
    FileOperations& operations_;
    int fd_ = -1;
};

class OutputStream {
public:
    OutputStream(int fd, bool gzip, const std::string& path, FileOperations& operations)
        : fd_(fd), gzip_(gzip), path_(path), operations_(operations) {
        if (gzip_) {
            const int result = ::deflateInit2(&stream_, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                                               16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            if (result != Z_OK) fail_zlib("Cannot initialize gzip output", path_, result, stream_);
            initialized_ = true;
        }
    }
    ~OutputStream() { if (initialized_) ::deflateEnd(&stream_); }
    OutputStream(const OutputStream&) = delete;
    OutputStream& operator=(const OutputStream&) = delete;

    void append(std::string_view bytes) {
        while (!bytes.empty()) {
            const auto size = std::min(bytes.size(), buffer_.size() - used_);
            std::memcpy(buffer_.data() + used_, bytes.data(), size);
            used_ += size;
            bytes.remove_prefix(size);
            if (used_ == buffer_.size()) flush();
        }
    }

    void finish() {
        flush();
        if (gzip_) {
            int result;
            do {
                stream_.next_out = compressed_.data();
                stream_.avail_out = static_cast<uInt>(compressed_.size());
                result = operations_.deflate_step(&stream_, Z_FINISH);
                if (result != Z_OK && result != Z_STREAM_END) {
                    fail_zlib("Cannot finalize gzip output", path_, result, stream_);
                }
                write_all(compressed_.data(), compressed_.size() - stream_.avail_out);
            } while (result != Z_STREAM_END);
            const int ended = ::deflateEnd(&stream_);
            initialized_ = false;
            if (ended != Z_OK) fail_zlib("Cannot close gzip output", path_, ended, stream_);
        }
    }
private:
    void write_all(const unsigned char* data, std::size_t size) {
        while (size != 0) {
            const auto written = operations_.write_fd(fd_, data, size);
            if (written < 0) {
                if (errno == EINTR) continue;
                fail_io("Cannot write output", path_);
            }
            if (written == 0) throw Error(kIoError, "Output write made no progress: " + path_);
            data += written;
            size -= static_cast<std::size_t>(written);
        }
    }
    void flush() {
        if (used_ == 0) return;
        if (!gzip_) {
            write_all(buffer_.data(), used_);
        } else {
            stream_.next_in = buffer_.data();
            stream_.avail_in = static_cast<uInt>(used_);
            while (stream_.avail_in != 0) {
                stream_.next_out = compressed_.data();
                stream_.avail_out = static_cast<uInt>(compressed_.size());
                const int result = operations_.deflate_step(&stream_, Z_NO_FLUSH);
                if (result != Z_OK) fail_zlib("Cannot compress output", path_, result, stream_);
                write_all(compressed_.data(), compressed_.size() - stream_.avail_out);
            }
        }
        used_ = 0;
    }
    int fd_;
    bool gzip_;
    std::string path_;
    FileOperations& operations_;
    std::array<unsigned char, kBufferSize> buffer_{};
    std::array<unsigned char, kBufferSize> compressed_{};
    std::size_t used_ = 0;
    z_stream stream_{};
    bool initialized_ = false;
};

void serialize(OutputStream& stream, std::span<const SequenceRecord> input,
               const Alignment& alignment, std::size_t width) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        stream.append(">");
        stream.append(input[i].header);
        stream.append("\n");
        const std::string_view row = alignment.rows[i];
        if (width == 0) {
            stream.append(row);
            stream.append("\n");
        } else {
            for (std::size_t offset = 0; offset < row.size();) {
                const auto count = std::min(width, row.size() - offset);
                stream.append(row.substr(offset, count));
                stream.append("\n");
                offset += count;
            }
        }
    }
    stream.finish();
}
}  // namespace

ssize_t InputOperations::read_fd(int fd, void* data, std::size_t size) noexcept {
    return ::read(fd, data, size);
}

int InputOperations::close_fd(int fd) noexcept { return ::close(fd); }

InputData read_input_with_operations(const std::string& path,
                                    InputOperations& operations) {
    DecodedInput reader(path, operations);
    InputData result;
    result.identity = reader.identity();
    SequenceRecord current;
    std::string line;
    std::size_t line_number = 0;
    bool seen_header = false;
    const auto fail = [&](const std::string& reason) {
        throw Error(kInputError, path + ": line " + std::to_string(line_number) +
                                    ", record " + std::to_string(result.records.size() + 1) + ": " + reason);
    };
    while (reader.line(line)) {
        increment(line_number);
        if (line.empty()) continue;
        if (line.front() == '>') {
            if (seen_header) {
                if (current.sequence.empty()) fail("Empty sequence");
                result.records.push_back(std::move(current));
                current = {};
            }
            seen_header = true;
            current.header = line.substr(1);
            if (std::none_of(current.header.begin(), current.header.end(),
                             [](unsigned char ch) { return !ascii_space(ch); })) {
                fail("Empty or whitespace-only FASTA header");
            }
        } else {
            if (!seen_header) fail("Expected a FASTA header beginning with '>'");
            for (unsigned char ch : line) {
                if (ch == '-' || ch == '.') fail("Input contains an existing gap; supply unaligned sequences");
                if (std::string_view("acgtun").find(static_cast<char>(ch)) != std::string_view::npos) {
                    increment(result.stats.uppercase_conversions);
                } else if (std::string_view("ACGTUN").find(static_cast<char>(ch)) == std::string_view::npos) {
                    increment(result.stats.unknown_replacements);
                }
                current.sequence.push_back(static_cast<char>(kClean[ch]));
            }
        }
    }
    if (!seen_header) fail("Input contains no FASTA records");
    if (current.sequence.empty()) fail("Empty sequence");
    result.records.push_back(std::move(current));
    reader.finish();
    return result;
}

InputData read_input(const std::string& path) {
    InputOperations operations;
    return read_input_with_operations(path, operations);
}

void check_output_path(const Options& options,
                       const std::optional<FileIdentity>& input_identity) {
    if (options.output == "-") return;
    // Compare actual file identities. Lexical normalization is incorrect for
    // paths containing a symlink followed by '..': the kernel follows the
    // symlink before resolving '..', which can name a different directory.
    struct stat target {};
    if (::stat(options.output.c_str(), &target) == 0) {
        if (input_identity && input_identity->device == static_cast<std::uintmax_t>(target.st_dev) &&
            input_identity->inode == static_cast<std::uintmax_t>(target.st_ino)) {
            throw Error(kCliError, "Input and output refer to the same file (including link aliases)");
        }
        if (!S_ISREG(target.st_mode)) {
            throw Error(kIoError, "Output must be a regular file: " + options.output);
        }
    } else if (errno != ENOENT) {
        fail_io("Cannot inspect output", options.output);
    }
    struct stat link {};
    if (::lstat(options.output.c_str(), &link) == 0) {
        if (!options.force) throw Error(kIoError, "Output already exists; use --force: " + options.output);
    } else if (errno != ENOENT) {
        fail_io("Cannot inspect output", options.output);
    }
    auto parent = std::filesystem::path(options.output).parent_path();
    if (parent.empty()) parent = ".";
    std::error_code ec;
    if (!std::filesystem::is_directory(parent, ec)) {
        throw Error(kIoError, "Output parent directory does not exist or is inaccessible: " + parent.string());
    }
}

ssize_t FileOperations::write_fd(int fd, const void* data, std::size_t size) noexcept {
    return ::write(fd, data, size);
}
int FileOperations::close_fd(int fd) noexcept { return ::close(fd); }
int FileOperations::deflate_step(z_stream* stream, int flush) noexcept { return ::deflate(stream, flush); }

int FileOperations::publish(const std::filesystem::path& temporary,
                            const std::filesystem::path& destination, bool replace) noexcept {
    if (replace) return ::rename(temporary.c_str(), destination.c_str());
#ifdef SYS_renameat2
    constexpr unsigned int no_replace = 1;  // RENAME_NOREPLACE from linux/fs.h.
    if (::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
                  destination.c_str(), no_replace) == 0) return 0;
    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) return -1;
#endif
    // link is also atomic and refuses to replace an existing destination. The
    // same-directory temporary file guarantees both names are on one filesystem.
    struct stat owned {};
    if (::lstat(temporary.c_str(), &owned) != 0) return -1;
    if (::link(temporary.c_str(), destination.c_str()) != 0) return -1;
    if (::unlink(temporary.c_str()) != 0) {
        // The output is already published. Do not report a failed transaction
        // or let its destructor blindly remove a name we no longer own. A
        // best-effort retry is limited to the original temporary inode.
        struct stat remaining {};
        if (::lstat(temporary.c_str(), &remaining) == 0 &&
            remaining.st_dev == owned.st_dev && remaining.st_ino == owned.st_ino) {
            ::unlink(temporary.c_str());
        }
    }
    return 0;
}

void write_output_with_operations(const Options& options,
                                  std::span<const SequenceRecord> input,
                                  const Alignment& alignment,
                                  const std::optional<FileIdentity>& input_identity,
                                  FileOperations& operations) {
    if (input.empty() || alignment.rows.size() != input.size()) {
        throw Error(kCoreError, "Cannot serialize an alignment with an invalid row count");
    }
    check_output_path(options, input_identity);
    if (options.output == "-") {
        OutputStream stream(STDOUT_FILENO, false, "standard output", operations);
        serialize(stream, input, alignment, options.line_width);
    } else {
        OutputTransaction transaction(options.output, options.force, operations);
        const bool gzip = options.output.ends_with(".gz");
        {
            OutputStream stream(transaction.fd(), gzip, options.output, operations);
            serialize(stream, input, alignment, options.line_width);
        }
        transaction.commit();
    }
}

void write_output(const Options& options, std::span<const SequenceRecord> input,
                  const Alignment& alignment,
                  const std::optional<FileIdentity>& input_identity) {
    FileOperations operations;
    write_output_with_operations(options, input, alignment, input_identity, operations);
}
}  // namespace msakit::detail
