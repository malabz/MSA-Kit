#include "internal.hpp"
#include <exception>
#include <kseq.h>
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
#include <CLI/CLI.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <charconv>
#include <csignal>
#include <iostream>
#include <memory>
#include <new>
#include <string>

// Byte streams, record parsing, cleaning and transactional output.
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
        std::ptrdiff_t count;
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

    // kseq receives the original decoded bytes, with no line preprocessing.
    std::size_t read(void* destination, std::size_t capacity) {
        if (capacity == 0) return 0;
        if (decoded_pos_ == decoded_size_) {
            decoded_size_ = fill();
            decoded_pos_ = 0;
        }
        const auto count = std::min(capacity, decoded_size_ - decoded_pos_);
        std::memcpy(destination, decoded_.data() + decoded_pos_, count);
        decoded_pos_ += count;
        return count;
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

// Never unwind through kseq's C allocation/initialization code. Retain the
// original typed exception so I/O, gzip and allocation failures keep their codes.
struct KseqSource {
    DecodedInput& input;
    std::exception_ptr failure;
};

int read_kseq_bytes(KseqSource* source, void* data, int size) noexcept {
    if (source->failure) return -1;
    try {
        return static_cast<int>(source->input.read(data, static_cast<std::size_t>(size)));
    } catch (...) {
        source->failure = std::current_exception();
        return -1;
    }
}

KSEQ_INIT(KseqSource*, read_kseq_bytes)

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

std::ptrdiff_t InputOperations::read_fd(int fd, void* data, std::size_t size) noexcept {
    return ::read(fd, data, size);
}

int InputOperations::close_fd(int fd) noexcept { return ::close(fd); }

InputData read_input_with_operations(const std::string& path,
                                    InputOperations& operations) {
    DecodedInput reader(path, operations);
    KseqSource source{reader, {}};
    std::unique_ptr<kseq_t, decltype(&kseq_destroy)> parser(kseq_init(&source), kseq_destroy);
    if (!parser) throw Error(kCoreError, "Cannot allocate kseq reader: " + path);
    InputData result;
    result.identity = reader.identity();
    const auto fail = [&](const std::string& reason) {
        throw Error(kInputError, path + ": record " +
                    std::to_string(result.records.size() + 1) + ": " + reason);
    };
    while (true) {
        const int status = kseq_read(parser.get());
        if (source.failure) std::rethrow_exception(source.failure);
        if (status == -1) break;
        if (status == -4) throw Error(kCoreError, "kseq allocation or length limit failure: " + path);
        if (status == -3) throw Error(kIoError, "Cannot read sequence input: " + path);
        if (status < 0) fail("FASTQ quality length mismatch");

        SequenceRecord current;
        // Same name/comment reconstruction as HAlign-5's KseqReader.
        if (parser->name.l) current.header.assign(parser->name.s, parser->name.l);
        if (parser->comment.l) {
            if (!current.header.empty()) current.header.push_back(' ');
            current.header.append(parser->comment.s, parser->comment.l);
        }
        if (std::none_of(current.header.begin(), current.header.end(),
                         [](unsigned char ch) { return !ascii_space(ch); })) {
            fail("empty or whitespace-only header");
        }
        if (parser->seq.l == 0) fail("empty sequence");
        current.sequence.reserve(parser->seq.l);
        for (std::size_t i = 0; i < parser->seq.l; ++i) {
            const auto ch = static_cast<unsigned char>(parser->seq.s[i]);
            if (ch == '-' || ch == '.') fail("existing gap; supply unaligned sequences");
            if (std::string_view("acgtun").find(static_cast<char>(ch)) != std::string_view::npos) {
                increment(result.stats.uppercase_conversions);
            } else if (std::string_view("ACGTUN").find(static_cast<char>(ch)) == std::string_view::npos) {
                increment(result.stats.unknown_replacements);
            }
            current.sequence.push_back(static_cast<char>(kClean[ch]));
        }
        result.records.push_back(std::move(current));
    }
    if (result.records.empty()) throw Error(kInputError, path + ": no sequence records");
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

std::ptrdiff_t FileOperations::write_fd(int fd, const void* data, std::size_t size) noexcept {
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

// Shared result contract validation.
namespace msakit::detail {

void validate_alignment(std::span<const SequenceRecord> input,
                        const Alignment& alignment) {
    if (input.empty() || alignment.rows.size() != input.size()) {
        throw Error(kCoreError, "alignment row count does not match nonempty input");
    }
    const auto width = alignment.rows.front().size();
    if (width == 0) throw Error(kCoreError, "alignment rows must not be empty");

    for (std::size_t row = 0; row < input.size(); ++row) {
        const auto& aligned = alignment.rows[row];
        if (aligned.size() != width) {
            throw Error(kCoreError, "alignment rows have different lengths at row " +
                        std::to_string(row + 1));
        }
        std::size_t consumed = 0;
        for (const char residue : aligned) {
            if (residue == '-') continue;
            if (residue != 'A' && residue != 'C' && residue != 'G' &&
                residue != 'T' && residue != 'U' && residue != 'N') {
                throw Error(kCoreError, "invalid alignment character at row " +
                            std::to_string(row + 1));
            }
            if (consumed == input[row].sequence.size() ||
                residue != input[row].sequence[consumed]) {
                throw Error(kCoreError, "alignment changes cleaned input residues at row " +
                            std::to_string(row + 1));
            }
            ++consumed;
        }
        if (consumed != input[row].sequence.size() || consumed == 0) {
            throw Error(kCoreError, "alignment loses cleaned input residues at row " +
                        std::to_string(row + 1));
        }
    }

    for (std::size_t column = 0; column < width; ++column) {
        bool contains_residue = false;
        for (const auto& row : alignment.rows) {
            if (row[column] != '-') {
                contains_residue = true;
                break;
            }
        }
        if (!contains_residue) {
            throw Error(kCoreError, "alignment contains an all-gap column at column " +
                        std::to_string(column + 1));
        }
    }
}

}  // namespace msakit::detail

// Command line and application error boundary.
#ifndef MSAKIT_VERSION
#define MSAKIT_VERSION "0.2.0"
#endif

namespace msakit::detail {
namespace {

// Reporting an allocation failure must not itself require an allocation.
void report_error(const char* message) noexcept {
    std::fprintf(stderr, "msakit: %s\n", message);
    std::fflush(stderr);
}

std::size_t parse_width(std::string_view text) {
    if (text.empty() || text.find_first_not_of("0123456789") != text.npos) {
        throw Error(kCliError, "--line-width must be a nonnegative decimal integer");
    }
    std::size_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw Error(kCliError, "--line-width is outside the supported integer range");
    }
    return result;
}

}  // namespace

int run_application(int argc, char** argv, CoreFunction core) {
    // Convert broken-pipe writes to an ordinary I/O error (exit 4).
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        report_error("failed to configure broken-pipe handling");
        return kCoreError;
    }

    try {
        Options options;
        std::string width_text = "80";
        CLI::App app{"DNA/RNA multiple sequence alignment with a replaceable reference core", "msakit"};
        app.set_version_flag("-v,--version", "msakit " MSAKIT_VERSION);
        app.add_option("-i,--input", options.input, "Input FASTA/FASTQ, optionally gzip; '-' reads stdin")
            ->required();
        app.add_option("-o,--output", options.output, "Output FASTA; '.gz' compresses; '-' writes plain stdout")
            ->required();
        app.add_option("--line-width", width_text, "Sequence line width; 0 disables wrapping")
            ->default_val("80");
        app.add_flag("--force", options.force, "Replace an existing output file after a complete write");
        app.add_flag("--quiet", options.quiet, "Suppress ordinary logs; errors remain visible");

        try {
            app.parse(argc, argv);
        } catch (const CLI::ParseError& error) {
            // CLI11 handles help/version formatting, but its numeric parse-error
            // codes are not MSA-Kit's public exit-code contract.
            const int parser_status = app.exit(error, std::cout, std::cerr);
            return parser_status == 0 ? 0 : kCliError;
        }

        options.line_width = parse_width(width_text);
        if (options.input.empty() || options.output.empty()) {
            throw Error(kCliError, "--input and --output must not be empty");
        }

        auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        spdlog::logger logger("msakit", std::move(sink));
        logger.set_pattern("[msakit] %v");
        logger.set_level(options.quiet ? spdlog::level::off : spdlog::level::info);

        InputData input = read_input(options.input);
        check_output_path(options, input.identity);
        logger.info("Read {} sequences; lowercase conversions: {}; replaced with N: {}",
                    input.records.size(), input.stats.uppercase_conversions,
                    input.stats.unknown_replacements);

        Alignment alignment;
        try {
            if (core == nullptr) {
                throw std::runtime_error("no alignment core was supplied");
            }
            alignment = core(input.records);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception& error) {
            throw Error(kCoreError, std::string("alignment core failed: ") + error.what());
        } catch (...) {
            throw Error(kCoreError, "alignment core failed with an unknown exception");
        }

        validate_alignment(input.records, alignment);
        logger.info("Validated {} alignment rows; writing output", alignment.rows.size());
        write_output(options, input.records, alignment, input.identity);
        // Publishing the completed file is the last fallible operation. Logging
        // afterward must not turn a successful replacement into a reported error.
        return 0;
    } catch (const Error& error) {
        report_error(error.what());
        return error.exit_code();
    } catch (const std::bad_alloc&) {
        report_error("memory allocation failed");
        return kCoreError;
    } catch (const std::exception& error) {
        report_error(error.what());
        return kCoreError;
    } catch (...) {
        report_error("unexpected internal failure");
        return kCoreError;
    }
}

}  // namespace msakit::detail
