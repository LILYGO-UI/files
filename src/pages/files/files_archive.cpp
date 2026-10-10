#include "pages/files/files_archive.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace files_archive {
namespace {

constexpr std::size_t kWindowSize   = 32768;
constexpr std::size_t kWindowMask   = kWindowSize - 1;
constexpr unsigned kHashBits        = 15;
constexpr unsigned kMinimumMatch    = 3;
constexpr unsigned kMaximumMatch    = 258;
constexpr unsigned kMaximumChain    = 64;
constexpr unsigned kEndOfBlock      = 256;
constexpr std::size_t kChunkSize    = 65536;
constexpr int kMaximumDepth         = 64;
constexpr std::uint64_t kZipLimit   = 0xffffffffU;  // Sizes and offsets without the ZIP64 extension.
constexpr std::size_t kEntryLimit   = 0xffff;
constexpr std::uint16_t kUtf8Names  = 0x0800;
constexpr std::uint16_t kVersion    = 20;
constexpr std::uint16_t kMadeByUnix = (3U << 8) | kVersion;
constexpr std::uint16_t kStored     = 0;
constexpr std::uint16_t kDeflated   = 8;

constexpr std::array<std::uint16_t, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistanceBase{1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistanceExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct HuffmanCode {
    std::uint16_t bits  = 0;  // Bit-reversed, ready to be written least significant bit first.
    std::uint8_t length = 0;
};

std::uint16_t reverse_bits(std::uint32_t code, unsigned length) noexcept
{
    std::uint32_t reversed = 0;
    for (unsigned bit = 0; bit < length; ++bit) reversed |= ((code >> bit) & 1U) << (length - 1U - bit);
    return static_cast<std::uint16_t>(reversed);
}

// The fixed literal/length code of RFC 1951, section 3.2.6.
std::array<HuffmanCode, 288> make_literal_codes() noexcept
{
    std::array<HuffmanCode, 288> codes{};
    for (unsigned symbol = 0; symbol < codes.size(); ++symbol) {
        std::uint32_t code = 0;
        unsigned length    = 0;
        if (symbol < 144) {
            code   = 0x30U + symbol;
            length = 8;
        } else if (symbol < 256) {
            code   = 0x190U + symbol - 144U;
            length = 9;
        } else if (symbol < 280) {
            code   = symbol - 256U;
            length = 7;
        } else {
            code   = 0xc0U + symbol - 280U;
            length = 8;
        }
        codes[symbol] = {reverse_bits(code, length), static_cast<std::uint8_t>(length)};
    }
    return codes;
}

std::array<std::uint32_t, 256> make_crc_table() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1U) != 0U ? 0xedb88320U ^ (value >> 1) : value >> 1;
        table[index] = value;
    }
    return table;
}

void put16(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
}

void put32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    put16(bytes, value & 0xffffU);
    put16(bytes, value >> 16);
}

// MS-DOS date and time fields; dates before 1980 cannot be represented and use its first day.
void dos_date_time(std::int64_t seconds, std::uint16_t &date, std::uint16_t &time) noexcept
{
    const auto value = static_cast<std::time_t>(seconds);
    std::tm local{};
    if (localtime_r(&value, &local) == nullptr || local.tm_year < 80) {
        date = (1U << 5) | 1U;
        time = 0;
        return;
    }
    const int year = std::min(local.tm_year - 80, 127);
    date           = static_cast<std::uint16_t>((year << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
    time           = static_cast<std::uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
}

class ZipWriter {
public:
    ZipWriter() = default;
    ~ZipWriter()
    {
        abandon();
    }

    ZipWriter(const ZipWriter &)            = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;

    bool create(const std::string &path)
    {
        fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd_ < 0) return fail_errno(errno);
        path_ = path;
        return true;
    }

    bool add(const std::string &path, const std::string &name, int depth)
    {
        struct stat stats{};
        if (lstat(path.c_str(), &stats) != 0) return fail_errno(errno);
        if (S_ISDIR(stats.st_mode)) return add_directory(path, name, stats, depth);
        if (S_ISLNK(stats.st_mode)) return add_symlink(path, name, stats);
        if (S_ISREG(stats.st_mode)) return add_file(path, name, stats);
        return true;  // Devices, sockets and pipes have no content to archive.
    }

    bool finish()
    {
        const std::uint64_t directory_start = offset_;
        std::vector<std::uint8_t> directory;
        for (const Entry &entry : entries_) {
            put32(directory, 0x02014b50U);
            put16(directory, kMadeByUnix);
            put16(directory, kVersion);
            put16(directory, kUtf8Names);
            put16(directory, entry.method);
            put16(directory, entry.time);
            put16(directory, entry.date);
            put32(directory, entry.crc);
            put32(directory, entry.compressed);
            put32(directory, entry.uncompressed);
            put16(directory, static_cast<std::uint32_t>(entry.name.size()));
            put16(directory, 0);  // Extra field length.
            put16(directory, 0);  // Comment length.
            put16(directory, 0);  // Starting disk.
            put16(directory, 0);  // Internal attributes.
            put32(directory, entry.attributes);
            put32(directory, entry.offset);
            directory.insert(directory.end(), entry.name.begin(), entry.name.end());
        }
        const std::size_t directory_size = directory.size();
        if (directory_start + directory_size >= kZipLimit) return fail("Archive is too large");

        put32(directory, 0x06054b50U);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, static_cast<std::uint32_t>(entries_.size()));
        put16(directory, static_cast<std::uint32_t>(entries_.size()));
        put32(directory, static_cast<std::uint32_t>(directory_size));
        put32(directory, static_cast<std::uint32_t>(directory_start));
        put16(directory, 0);
        if (!write_bytes(directory.data(), directory.size())) return false;

        const int descriptor = fd_;
        fd_                  = -1;
        if (close(descriptor) != 0) {
            fail_errno(errno);
            unlink(path_.c_str());
            path_.clear();
            return false;
        }
        path_.clear();
        return true;
    }

    // Removes the partial archive after a failure.
    void abandon() noexcept
    {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
        if (!path_.empty()) unlink(path_.c_str());
        path_.clear();
    }

    [[nodiscard]] const std::string &error() const noexcept
    {
        return error_;
    }

private:
    struct Entry {
        std::string name;
        std::uint16_t method       = kStored;
        std::uint16_t date         = 0;
        std::uint16_t time         = 0;
        std::uint32_t crc          = 0;
        std::uint32_t compressed   = 0;
        std::uint32_t uncompressed = 0;
        std::uint32_t attributes   = 0;
        std::uint32_t offset       = 0;
    };

    static Entry make_entry(std::string name, const struct stat &stats, std::uint16_t method)
    {
        Entry entry;
        entry.name   = std::move(name);
        entry.method = method;
        // Unix permissions and file type live in the upper half; 0x10 is the MS-DOS directory flag.
        entry.attributes = (static_cast<std::uint32_t>(stats.st_mode) << 16) | (S_ISDIR(stats.st_mode) ? 0x10U : 0U);
        dos_date_time(static_cast<std::int64_t>(stats.st_mtime), entry.date, entry.time);
        return entry;
    }

    bool add_directory(const std::string &path, const std::string &name, const struct stat &stats, int depth)
    {
        if (depth > kMaximumDepth) return fail("Folders are nested too deeply");
        Entry entry = make_entry(name + '/', stats, kStored);
        if (!begin_entry(entry) || !end_entry(entry)) return false;

        DIR *directory = opendir(path.c_str());
        if (directory == nullptr) return fail_errno(errno);
        std::vector<std::string> children;
        int read_error = 0;
        for (;;) {
            errno              = 0;
            const dirent *item = readdir(directory);
            if (item == nullptr) {
                read_error = errno;
                break;
            }
            if (std::strcmp(item->d_name, ".") != 0 && std::strcmp(item->d_name, "..") != 0) {
                children.emplace_back(item->d_name);
            }
        }
        closedir(directory);
        if (read_error != 0) return fail_errno(read_error);

        std::sort(children.begin(), children.end());
        for (const std::string &child : children) {
            if (!add(path + '/' + child, name + '/' + child, depth + 1)) return false;
        }
        return true;
    }

    bool add_symlink(const std::string &path, const std::string &name, const struct stat &stats)
    {
        // Like Info-ZIP, the link is stored with its file type and the target path as its contents.
        std::vector<char> target(PATH_MAX);
        const ssize_t length = readlink(path.c_str(), target.data(), target.size());
        if (length < 0) return fail_errno(errno);

        Entry entry        = make_entry(name, stats, kStored);
        const auto *data   = reinterpret_cast<const std::uint8_t *>(target.data());
        const auto size    = static_cast<std::size_t>(length);
        entry.crc          = crc32(0, data, size);
        entry.compressed   = static_cast<std::uint32_t>(size);
        entry.uncompressed = static_cast<std::uint32_t>(size);
        return begin_entry(entry) && write_bytes(data, size) && end_entry(entry);
    }

    bool add_file(const std::string &path, const std::string &name, const struct stat &stats)
    {
        if (stats.st_size < 0 || static_cast<std::uint64_t>(stats.st_size) >= kZipLimit) {
            return fail("A file is too large to compress");
        }
        const int source = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (source < 0) return fail_errno(errno);
        const bool added = add_file_contents(source, name, stats);
        close(source);
        return added;
    }

    bool add_file_contents(int source, const std::string &name, const struct stat &stats)
    {
        Entry entry = make_entry(name, stats, kDeflated);
        if (!begin_entry(entry)) return false;
        const std::uint64_t data_start = offset_;

        std::uint32_t crc          = 0;
        std::uint64_t uncompressed = 0;
        encoder_.reset();
        buffer_.resize(kChunkSize);
        for (;;) {
            std::size_t length = 0;
            if (!read_chunk(source, length)) return false;
            if (length == 0) break;
            crc = crc32(crc, buffer_.data(), length);
            uncompressed += length;
            if (uncompressed >= kZipLimit) return fail("A file is too large to compress");
            encoder_.write(buffer_.data(), length);
            if (!flush_encoder()) return false;
        }
        encoder_.finish();
        if (!flush_encoder()) return false;

        std::uint64_t compressed = offset_ - data_start;
        if (compressed >= uncompressed) {
            // Data that does not shrink, such as media or archives, is stored as is.
            if (!rewind(data_start) || lseek(source, 0, SEEK_SET) < 0) return fail_errno(errno);
            std::uint64_t copied = 0;
            for (;;) {
                std::size_t length = 0;
                if (!read_chunk(source, length)) return false;
                if (length == 0) break;
                copied += length;
                if (copied > uncompressed) return fail("A file changed while it was compressed");
                if (!write_bytes(buffer_.data(), length)) return false;
            }
            if (copied != uncompressed) return fail("A file changed while it was compressed");
            entry.method = kStored;
            compressed   = uncompressed;
        }
        entry.crc          = crc;
        entry.compressed   = static_cast<std::uint32_t>(compressed);
        entry.uncompressed = static_cast<std::uint32_t>(uncompressed);
        return end_entry(entry);
    }

    bool read_chunk(int source, std::size_t &length)
    {
        for (;;) {
            const ssize_t result = read(source, buffer_.data(), buffer_.size());
            if (result >= 0) {
                length = static_cast<std::size_t>(result);
                return true;
            }
            if (errno != EINTR) return fail_errno(errno);
        }
    }

    bool flush_encoder()
    {
        std::vector<std::uint8_t> &output = encoder_.output();
        const bool written                = write_bytes(output.data(), output.size());
        output.clear();
        return written;
    }

    bool rewind(std::uint64_t offset)
    {
        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) < 0) return false;
        if (ftruncate(fd_, static_cast<off_t>(offset)) != 0) return false;
        offset_ = offset;
        return true;
    }

    // Writes the local header; end_entry() fills in the method, checksum and sizes once they are known.
    bool begin_entry(Entry &entry)
    {
        if (entries_.size() >= kEntryLimit) return fail("Too many items to compress");
        if (entry.name.size() > 0xffffU) return fail("A name is too long to compress");
        if (offset_ >= kZipLimit) return fail("Archive is too large");
        entry.offset = static_cast<std::uint32_t>(offset_);

        std::vector<std::uint8_t> header;
        header.reserve(30U + entry.name.size());
        put32(header, 0x04034b50U);
        put16(header, kVersion);
        put16(header, kUtf8Names);
        put16(header, entry.method);
        put16(header, entry.time);
        put16(header, entry.date);
        put32(header, entry.crc);
        put32(header, entry.compressed);
        put32(header, entry.uncompressed);
        put16(header, static_cast<std::uint32_t>(entry.name.size()));
        put16(header, 0);
        header.insert(header.end(), entry.name.begin(), entry.name.end());
        return write_bytes(header.data(), header.size());
    }

    bool end_entry(const Entry &entry)
    {
        if (offset_ >= kZipLimit) return fail("Archive is too large");
        std::vector<std::uint8_t> method;
        put16(method, entry.method);
        std::vector<std::uint8_t> sizes;
        put32(sizes, entry.crc);
        put32(sizes, entry.compressed);
        put32(sizes, entry.uncompressed);
        if (!patch(entry.offset + 8U, method) || !patch(entry.offset + 14U, sizes)) return false;
        entries_.push_back(entry);
        return true;
    }

    bool patch(std::uint64_t offset, const std::vector<std::uint8_t> &bytes)
    {
        std::size_t written = 0;
        while (written < bytes.size()) {
            const ssize_t result =
                pwrite(fd_, bytes.data() + written, bytes.size() - written, static_cast<off_t>(offset + written));
            if (result < 0) {
                if (errno == EINTR) continue;
                return fail_errno(errno);
            }
            written += static_cast<std::size_t>(result);
        }
        return true;
    }

    bool write_bytes(const std::uint8_t *data, std::size_t size)
    {
        std::size_t written = 0;
        while (written < size) {
            const ssize_t result = ::write(fd_, data + written, size - written);
            if (result < 0) {
                if (errno == EINTR) continue;
                return fail_errno(errno);
            }
            written += static_cast<std::size_t>(result);
        }
        offset_ += size;
        return true;
    }

    bool fail(std::string message)
    {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    bool fail_errno(int error_number)
    {
        return fail(std::strerror(error_number));
    }

    int fd_ = -1;
    std::string path_;
    std::uint64_t offset_ = 0;
    std::vector<Entry> entries_;
    DeflateEncoder encoder_;
    std::vector<std::uint8_t> buffer_;
    std::string error_;
};

}  // namespace

std::uint32_t crc32(std::uint32_t crc, const std::uint8_t *data, std::size_t size) noexcept
{
    static const std::array<std::uint32_t, 256> table = make_crc_table();
    crc                                               = ~crc;
    for (std::size_t index = 0; index < size; ++index) crc = table[(crc ^ data[index]) & 0xffU] ^ (crc >> 8);
    return ~crc;
}

DeflateEncoder::DeflateEncoder() : head_(std::size_t{1} << kHashBits), previous_(kWindowSize)
{
}

void DeflateEncoder::reset()
{
    window_.clear();
    window_start_ = 0;
    position_     = 0;
    std::fill(head_.begin(), head_.end(), 0U);
    std::fill(previous_.begin(), previous_.end(), 0U);
    output_.clear();
    bit_buffer_ = 0;
    bit_count_  = 0;
    started_    = false;
}

void DeflateEncoder::write(const std::uint8_t *data, std::size_t size)
{
    start();
    window_.insert(window_.end(), data, data + size);
    encode(false);
}

void DeflateEncoder::finish()
{
    start();
    encode(true);
    put_symbol(kEndOfBlock);
    // The data is one open-ended block; an empty final block terminates the stream.
    put_bits(1, 1);
    put_bits(1, 2);
    put_symbol(kEndOfBlock);
    if (bit_count_ > 0) output_.push_back(static_cast<std::uint8_t>(bit_buffer_ & 0xffU));
    bit_buffer_ = 0;
    bit_count_  = 0;
}

std::vector<std::uint8_t> &DeflateEncoder::output() noexcept
{
    return output_;
}

void DeflateEncoder::start()
{
    if (started_) return;
    put_bits(0, 1);  // Not the final block.
    put_bits(1, 2);  // Fixed Huffman codes.
    started_ = true;
}

void DeflateEncoder::encode(bool final)
{
    const std::size_t end = window_.size();
    while (position_ < end) {
        const std::size_t available = end - position_;
        // Keep a full match length of lookahead until the input ends.
        if (!final && available < kMaximumMatch) break;

        unsigned best_length   = 0;
        unsigned best_distance = 0;
        if (available >= kMinimumMatch) {
            const std::uint64_t absolute = window_start_ + position_;
            const auto limit             = static_cast<unsigned>(std::min<std::size_t>(available, kMaximumMatch));
            std::uint64_t candidate      = head_[hash(position_)];
            for (unsigned chain = 0; candidate != 0U && chain < kMaximumChain; ++chain) {
                const std::uint64_t start = candidate - 1U;
                if (start < window_start_ || absolute - start > kWindowSize) break;
                const std::uint8_t *current  = window_.data() + position_;
                const std::uint8_t *previous = window_.data() + (start - window_start_);
                unsigned length              = 0;
                while (length < limit && current[length] == previous[length]) ++length;
                if (length > best_length) {
                    best_length   = length;
                    best_distance = static_cast<unsigned>(absolute - start);
                    if (length == limit) break;
                }
                const std::uint64_t next = previous_[start & kWindowMask];
                if (next >= candidate) break;
                candidate = next;
            }
            insert(position_);
        }

        if (best_length >= kMinimumMatch) {
            put_match(best_length, best_distance);
            for (unsigned offset = 1; offset < best_length; ++offset) {
                if (position_ + offset + kMinimumMatch <= end) insert(position_ + offset);
            }
            position_ += best_length;
        } else {
            put_symbol(window_[position_]);
            ++position_;
        }
    }

    // Keep one window of history for later matches and drop older input.
    if (position_ > 2U * kWindowSize) {
        const std::size_t discard = position_ - kWindowSize;
        window_.erase(window_.begin(), window_.begin() + static_cast<std::ptrdiff_t>(discard));
        window_start_ += discard;
        position_ -= discard;
    }
}

void DeflateEncoder::insert(std::size_t position) noexcept
{
    const std::uint32_t key           = hash(position);
    const std::uint64_t absolute      = window_start_ + position;
    previous_[absolute & kWindowMask] = head_[key];
    head_[key]                        = absolute + 1U;
}

std::uint32_t DeflateEncoder::hash(std::size_t position) const noexcept
{
    const std::uint32_t value = (static_cast<std::uint32_t>(window_[position]) << 16) |
                                (static_cast<std::uint32_t>(window_[position + 1U]) << 8) | window_[position + 2U];
    return (value * 2654435761U) >> (32U - kHashBits);
}

void DeflateEncoder::put_bits(std::uint32_t value, unsigned count)
{
    bit_buffer_ |= static_cast<std::uint64_t>(value) << bit_count_;
    bit_count_ += count;
    while (bit_count_ >= 8U) {
        output_.push_back(static_cast<std::uint8_t>(bit_buffer_ & 0xffU));
        bit_buffer_ >>= 8;
        bit_count_ -= 8U;
    }
}

void DeflateEncoder::put_symbol(unsigned symbol)
{
    static const std::array<HuffmanCode, 288> codes = make_literal_codes();
    put_bits(codes[symbol].bits, codes[symbol].length);
}

void DeflateEncoder::put_match(unsigned length, unsigned distance)
{
    std::size_t code = kLengthBase.size() - 1U;
    while (kLengthBase[code] > length) --code;
    put_symbol(257U + static_cast<unsigned>(code));
    put_bits(length - kLengthBase[code], kLengthExtra[code]);

    code = kDistanceBase.size() - 1U;
    while (kDistanceBase[code] > distance) --code;
    put_bits(reverse_bits(static_cast<std::uint32_t>(code), 5), 5);
    put_bits(distance - kDistanceBase[code], kDistanceExtra[code]);
}

std::string create_zip(const std::string &archive_path, const std::vector<ZipSource> &sources)
{
    ZipWriter writer;
    if (!writer.create(archive_path)) return writer.error();
    bool written = true;
    for (std::size_t index = 0; written && index < sources.size(); ++index) {
        written = writer.add(sources[index].path, sources[index].name, 0);
    }
    if (!written || !writer.finish()) {
        writer.abandon();
        return writer.error().empty() ? std::string("Unknown error") : writer.error();
    }
    return {};
}

}  // namespace files_archive
