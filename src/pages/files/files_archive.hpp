#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_ARCHIVE_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_ARCHIVE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace files_archive {

// Continues a CRC-32 (the ZIP polynomial) over more data; start with 0.
[[nodiscard]] std::uint32_t crc32(std::uint32_t crc, const std::uint8_t *data, std::size_t size) noexcept;

// Streaming raw DEFLATE encoder (RFC 1951) using LZ77 matching and the fixed Huffman codes.
class DeflateEncoder {
public:
    DeflateEncoder();

    // Starts a new stream and drops pending output.
    void reset();
    void write(const std::uint8_t *data, std::size_t size);
    // Encodes the remaining input and terminates the stream.
    void finish();
    // Compressed bytes produced so far; callers may consume and clear them between writes.
    [[nodiscard]] std::vector<std::uint8_t> &output() noexcept;

private:
    void start();
    void encode(bool final);
    void insert(std::size_t position) noexcept;
    [[nodiscard]] std::uint32_t hash(std::size_t position) const noexcept;
    void put_bits(std::uint32_t value, unsigned count);
    void put_symbol(unsigned symbol);
    void put_match(unsigned length, unsigned distance);

    std::vector<std::uint8_t> window_;
    std::uint64_t window_start_ = 0;
    std::size_t position_       = 0;
    std::vector<std::uint64_t> head_;
    std::vector<std::uint64_t> previous_;
    std::vector<std::uint8_t> output_;
    std::uint64_t bit_buffer_ = 0;
    unsigned bit_count_       = 0;
    bool started_             = false;
};

// One top-level item of an archive: a file, a folder with its contents, or a symbolic link, which is
// stored rather than followed, kept in the archive under name.
struct ZipSource {
    std::string path;
    std::string name;
};

// Writes a ZIP archive at archive_path that holds the sources in order. The archive is created exclusively
// and removed again on failure. Returns an empty string on success, otherwise the reason it failed.
[[nodiscard]] std::string create_zip(const std::string &archive_path, const std::vector<ZipSource> &sources);

}  // namespace files_archive

#endif
