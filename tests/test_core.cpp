#include "app_config.hpp"
#include "pages/files/files_archive.hpp"
#include "pages/files/files_model.hpp"

#include "test_fixture.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

constexpr std::string_view kTruncationMarker = "[Preview truncated]";

void test_file_browsing_and_boundaries()
{
    TemporaryDirectory fixture("model");
    TemporaryDirectory outside("outside");
    fixture.make_directory("Documents/Nested");
    fixture.make_directory("Downloads");
    fixture.write_file("Documents/inside.txt", "inside\n");
    fixture.write_file("Documents/.hidden-inside", "not counted\n");
    fixture.write_file("README.txt", "LILYGO files\n");
    fixture.write_file("firmware.bin", "\x01\x02\x03");
    fixture.write_file(".hidden", "not listed\n");

    const std::string long_name(240, 'n');
    fixture.write_file(long_name + ".txt", "long filename\n");
    outside.write_file("outside.txt", "outside root\n");
    assert(::symlink(outside.root().c_str(), fixture.path("escape-link").c_str()) == 0);

    FilesModel model;
    const FilesCommandResult initialized = model.initialize(fixture.root().string());
    assert(initialized.succeeded());
    assert(model.initialized());
    assert(model.screen() == FilesScreen::Home);
    assert(model.contentScreen() == FilesScreen::Home);
    assert(model.displayPath() == "~/");
    assert(model.directoryName() == "Files");
    assert(model.entryCount() == 6);
    assert(model.entry(model.entryCount()) == nullptr);

    assert(model.entry(0) != nullptr && model.entry(0)->is_directory);
    assert(model.entry(1) != nullptr && model.entry(1)->is_directory);
    assert(model.entry(find_entry(model, "Downloads"))->kind == FilesEntryKind::Folder);

    // Folders carry the number of entries they would list; hidden entries are not counted.
    const FilesEntry *documents = model.entry(find_entry(model, "Documents"));
    assert(documents->item_count.has_value() && *documents->item_count == 2U);
    const FilesEntry *downloads = model.entry(find_entry(model, "Downloads"));
    assert(downloads->item_count.has_value() && *downloads->item_count == 0U);
    const FilesEntry *readme = model.entry(find_entry(model, "README.txt"));
    assert(!readme->item_count.has_value());
    assert(readme->modified_time > 0);
    assert(model.entry(find_entry(model, long_name + ".txt"))->name == long_name + ".txt");

    const auto escape_index = find_entry(model, "escape-link");
    assert(model.entry(escape_index)->is_symlink);
    const FilesCommandResult escaped = model.openEntry(escape_index);
    assert(!escaped.succeeded());
    assert(escaped.status() == FilesCommandStatus::Unsupported);
    assert(model.screen() == FilesScreen::Home);
    assert(model.displayPath() == "~/");

    const auto documents_index = find_entry(model, "Documents");
    assert(model.openEntry(documents_index).succeeded());
    assert(model.screen() == FilesScreen::Folder);
    assert(model.contentScreen() == FilesScreen::Folder);
    assert(model.displayPath() == "Documents");
    assert(model.directoryName() == "Documents");
    assert(model.entryCount() == 2);

    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(model.displayPath() == "~/");
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(model.displayPath() == "~/");
    const FilesCommandResult invalid_folder = model.show(FilesScreen::Folder);
    assert(!invalid_folder.succeeded());
    assert(invalid_folder.status() == FilesCommandStatus::InvalidArgument);
}

void test_search_creation_and_bounded_text()
{
    TemporaryDirectory fixture("search");
    TemporaryDirectory outside("create-boundary");
    fixture.make_directory("Documents");
    fixture.make_directory("Downloads");
    fixture.write_file("Device-Note.TXT", "preview text\n");
    fixture.write_file("other.log", "other\n");

    FilesModel model(fixture.root().string());
    assert(model.initialized());
    model.setQuery("note");
    assert(model.query() == "note");
    assert(model.searchCount() == 1);
    assert(model.searchEntry(0) != nullptr);
    assert(model.searchEntry(0)->name == "Device-Note.TXT");
    assert(model.searchEntry(1) == nullptr);
    assert(model.openSearchEntry(0).succeeded());
    assert(model.screen() == FilesScreen::Preview);
    assert(model.preview().find("preview text") != std::string::npos);
    assert(model.back().succeeded());

    model.setFolderName("Projects");
    assert(model.createFolder().succeeded());
    assert(std::filesystem::is_directory(fixture.path("Projects")));
    assert(model.entry(find_entry(model, "Projects"))->is_directory);
    assert(model.folderName() == FilesModel::default_folder_name);

    const std::string escaped_name = "../" + outside.root().filename().string() + "/created";
    assert(!std::filesystem::exists(outside.path("created")));
    model.setFolderName(escaped_name);
    const FilesCommandResult escaped = model.createFolder();
    assert(!escaped.succeeded());
    assert(escaped.status() == FilesCommandStatus::InvalidArgument);
    assert(model.folderName() == escaped_name);
    assert(!std::filesystem::exists(outside.path("created")));

    model.setFolderName("Projects");
    const FilesCommandResult duplicate = model.createFolder();
    assert(!duplicate.succeeded());
    assert(duplicate.status() == FilesCommandStatus::IoError);
    assert(model.folderName() == "Projects");

    assert(model.show(FilesScreen::NewFolder).succeeded());
    model.setFolderName("cancelled-draft");
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(model.folderName() == FilesModel::default_folder_name);

    const std::string long_text(300, 'x');
    model.setQuery(long_text);
    model.setFolderName(long_text);
    assert(model.query().size() == FilesModel::text_capacity - 1U);
    assert(model.folderName().size() == FilesModel::text_capacity - 1U);

    std::string utf8_text;
    for (int index = 0; index < 40; ++index) utf8_text += "目录";
    model.setQuery(utf8_text);
    assert(model.query().size() == FilesModel::text_capacity - 1U);
    assert(utf8_text.compare(0, model.query().size(), model.query()) == 0);
}

void test_large_preview_is_truncated()
{
    TemporaryDirectory fixture("preview");
    std::string large_text;
    large_text.reserve(12000);
    for (int line = 0; line < 1000; ++line) {
        large_text += "preview-line-" + std::to_string(line) + "\n";
    }
    assert(large_text.size() > FilesModel::preview_capacity);
    fixture.write_file("large-preview.txt", large_text);
    fixture.write_file("binary.bin", std::string_view("binary\0payload", 14));

    FilesModel model(fixture.root().string());
    assert(model.initialized());
    assert(model.openEntry(find_entry(model, "large-preview.txt")).succeeded());
    assert(model.screen() == FilesScreen::Preview);
    assert(model.previewEntry() != nullptr);
    assert(model.previewEntry()->name == "large-preview.txt");
    assert(model.preview().size() <= FilesModel::preview_capacity - 1U);
    assert(model.preview().find("preview-line-0") == 0U);
    assert(model.preview().size() >= kTruncationMarker.size());
    assert(model.preview().compare(model.preview().size() - kTruncationMarker.size(), kTruncationMarker.size(),
                                   kTruncationMarker) == 0);

    assert(model.back().succeeded());
    assert(model.openEntry(find_entry(model, "binary.bin")).succeeded());
    assert(model.preview() == "Preview is not available for this file type.");
}

void set_modified(const std::filesystem::path &path, std::time_t seconds)
{
    const timeval times[2] = {{seconds, 0}, {seconds, 0}};
    assert(::utimes(path.c_str(), times) == 0);
}

std::vector<std::string> entry_names(const FilesModel &model)
{
    std::vector<std::string> names;
    for (std::size_t index = 0; index < model.entryCount(); ++index) names.push_back(model.entry(index)->name);
    return names;
}

void test_sorting_and_view_mode()
{
    TemporaryDirectory fixture("sort");
    fixture.write_file("alpha/one.txt", "1\n");
    fixture.write_file("alpha/two.txt", "2\n");
    fixture.make_directory("Beta");
    fixture.write_file("b.txt", std::string(300, 'b'));
    fixture.write_file("a.pdf", std::string(10, 'a'));
    fixture.write_file("c.md", std::string(50, 'c'));
    constexpr std::time_t day  = 24 * 60 * 60;
    constexpr std::time_t base = 1767225600;
    set_modified(fixture.path("b.txt"), base);
    set_modified(fixture.path("c.md"), base + 31 * day);
    set_modified(fixture.path("a.pdf"), base + 59 * day);
    set_modified(fixture.path("alpha"), base + 90 * day);
    set_modified(fixture.path("Beta"), base + 120 * day);

    using Names = std::vector<std::string>;
    FilesModel model(fixture.root().string());
    assert(model.initialized());
    assert(model.sortKey() == FilesSortKey::Name && model.sortAscending());
    assert(model.viewMode() == FilesViewMode::Icons);
    assert(entry_names(model) == (Names{"alpha", "Beta", "a.pdf", "b.txt", "c.md"}));

    // Choosing the active key reverses it; folders stay ahead of files for every key.
    assert(model.setSortKey(FilesSortKey::Name).succeeded());
    assert(!model.sortAscending());
    assert(entry_names(model) == (Names{"Beta", "alpha", "c.md", "b.txt", "a.pdf"}));

    assert(model.setSortKey(FilesSortKey::Kind).succeeded());
    assert(model.sortAscending());
    assert(entry_names(model) == (Names{"alpha", "Beta", "c.md", "a.pdf", "b.txt"}));

    assert(model.setSortKey(FilesSortKey::Date).succeeded());
    assert(!model.sortAscending());
    assert(entry_names(model) == (Names{"Beta", "alpha", "a.pdf", "c.md", "b.txt"}));
    assert(model.setSortKey(FilesSortKey::Date).succeeded());
    assert(model.sortAscending());
    assert(entry_names(model) == (Names{"alpha", "Beta", "b.txt", "c.md", "a.pdf"}));

    // Sizes start with the largest; folders compare by the number of items they contain.
    assert(model.setSortKey(FilesSortKey::Size).succeeded());
    assert(!model.sortAscending());
    assert(entry_names(model) == (Names{"alpha", "Beta", "b.txt", "c.md", "a.pdf"}));
    assert(model.setSortKey(FilesSortKey::Size).succeeded());
    assert(entry_names(model) == (Names{"Beta", "alpha", "a.pdf", "c.md", "b.txt"}));

    // The order applies to folders opened later, and equal keys fall back to names.
    assert(model.openEntry(find_entry(model, "alpha")).succeeded());
    assert(entry_names(model) == (Names{"one.txt", "two.txt"}));
    assert(model.back().succeeded());

    const FilesCommandResult invalid_sort = model.setSortKey(static_cast<FilesSortKey>(99));
    assert(!invalid_sort.succeeded());
    assert(invalid_sort.status() == FilesCommandStatus::InvalidArgument);
    assert(model.sortKey() == FilesSortKey::Size);

    assert(model.actionCapability(FilesAction::ChangeSort).supported);
    assert(model.actionCapability(FilesAction::ChangeView).enabled);
    assert(model.show(FilesScreen::More).succeeded());
    assert(model.setViewMode(FilesViewMode::List).succeeded());
    assert(model.viewMode() == FilesViewMode::List);
    assert(model.screen() == FilesScreen::Home);
    assert(!model.setViewMode(static_cast<FilesViewMode>(7)).succeeded());
    assert(model.viewMode() == FilesViewMode::List);
}

void test_selection_mode()
{
    TemporaryDirectory fixture("select");
    fixture.make_directory("Folder");
    fixture.write_file("first.txt", "1\n");
    fixture.write_file("second.txt", "2\n");

    FilesModel model(fixture.root().string());
    assert(model.show(FilesScreen::Select).succeeded());
    assert(model.screen() == FilesScreen::Select);
    assert(model.selectedCount() == 0U);

    // A tap selects instead of opening, including on folders.
    const std::size_t folder = find_entry(model, "Folder");
    assert(model.activateEntry(folder).succeeded());
    assert(model.screen() == FilesScreen::Select);
    assert(model.isSelected(folder));
    assert(model.activateEntry(folder).succeeded());
    assert(!model.isSelected(folder));
    assert(!model.activateEntry(FilesModel::maximum_entries).succeeded());

    model.toggleSelectAll();
    assert(model.selectedCount() == model.entryCount());
    model.toggleSelectAll();
    assert(model.selectedCount() == 0U);

    assert(model.activateEntry(find_entry(model, "first.txt")).succeeded());
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(model.selectedCount() == 0U);

    // Outside selection mode a tap opens the entry.
    assert(model.activateEntry(find_entry(model, "Folder")).succeeded());
    assert(model.screen() == FilesScreen::Folder);
}

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Decodes the stored and fixed-Huffman DEFLATE blocks that the archive writer produces.
class Inflater {
public:
    Inflater(const std::uint8_t *data, std::size_t size) : data_(data), size_(size)
    {
    }

    std::string run()
    {
        static constexpr std::array<unsigned, 29> length_base{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                              15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                              67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::array<unsigned, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::array<unsigned, 30> distance_base{
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::array<unsigned, 30> distance_extra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        std::string output;
        for (bool final = false; !final;) {
            final               = bits(1) == 1U;
            const unsigned type = bits(2);
            if (type == 0U) {
                position_           = (position_ + 7U) & ~std::size_t{7};
                const unsigned size = bits(16);
                assert(bits(16) == (~size & 0xffffU));
                for (unsigned index = 0; index < size; ++index) output.push_back(static_cast<char>(bits(8)));
                continue;
            }
            assert(type == 1U);
            for (;;) {
                const unsigned symbol = literal();
                if (symbol < 256U) {
                    output.push_back(static_cast<char>(symbol));
                    continue;
                }
                if (symbol == 256U) break;
                const unsigned length_code = symbol - 257U;
                assert(length_code < length_base.size());
                const unsigned length  = length_base[length_code] + bits(length_extra[length_code]);
                unsigned distance_code = 0;
                for (int bit = 0; bit < 5; ++bit) distance_code = (distance_code << 1) | bits(1);
                assert(distance_code < distance_base.size());
                const unsigned distance = distance_base[distance_code] + bits(distance_extra[distance_code]);
                assert(distance <= output.size());
                for (unsigned index = 0; index < length; ++index) output.push_back(output[output.size() - distance]);
            }
        }
        return output;
    }

private:
    unsigned bits(unsigned count)
    {
        unsigned value = 0;
        for (unsigned bit = 0; bit < count; ++bit, ++position_) {
            assert(position_ / 8U < size_);
            value |= ((data_[position_ / 8U] >> (position_ % 8U)) & 1U) << bit;
        }
        return value;
    }

    // Reads one fixed literal/length code, most significant bit first.
    unsigned literal()
    {
        unsigned code = 0;
        for (unsigned length = 1; length <= 9U; ++length) {
            code = (code << 1) | bits(1);
            if (length == 7U && code <= 0x17U) return 256U + code;
            if (length == 8U && code >= 0x30U && code <= 0xbfU) return code - 0x30U;
            if (length == 8U && code >= 0xc0U && code <= 0xc7U) return 280U + code - 0xc0U;
            if (length == 9U && code >= 0x190U) return 144U + code - 0x190U;
        }
        assert(false && "invalid literal code");
        return 0;
    }

    const std::uint8_t *data_;
    std::size_t size_;
    std::size_t position_ = 0;
};

std::uint32_t little_endian(const std::string &bytes, std::size_t offset, std::size_t size)
{
    assert(offset + size <= bytes.size());
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < size; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index])) << (8U * index);
    }
    return value;
}

struct ZipEntry {
    std::string contents;
    std::uint32_t mode = 0;
};

// Reads every entry through the central directory and checks its checksum.
std::map<std::string, ZipEntry> read_zip(const std::filesystem::path &path)
{
    const std::string bytes = read_file(path);
    assert(bytes.size() >= 22U);
    const std::size_t end = bytes.size() - 22U;
    assert(little_endian(bytes, end, 4) == 0x06054b50U);
    const std::uint32_t count = little_endian(bytes, end + 10U, 2);
    std::size_t offset        = little_endian(bytes, end + 16U, 4);
    assert(offset + little_endian(bytes, end + 12U, 4) == end);

    std::map<std::string, ZipEntry> entries;
    for (std::uint32_t index = 0; index < count; ++index) {
        assert(little_endian(bytes, offset, 4) == 0x02014b50U);
        const std::uint32_t method       = little_endian(bytes, offset + 10U, 2);
        const std::uint32_t crc          = little_endian(bytes, offset + 16U, 4);
        const std::uint32_t compressed   = little_endian(bytes, offset + 20U, 4);
        const std::uint32_t uncompressed = little_endian(bytes, offset + 24U, 4);
        const std::uint32_t name_length  = little_endian(bytes, offset + 28U, 2);
        const std::uint32_t local        = little_endian(bytes, offset + 42U, 4);
        const std::string name           = bytes.substr(offset + 46U, name_length);
        assert(little_endian(bytes, local, 4) == 0x04034b50U);
        assert(little_endian(bytes, local + 8U, 2) == method);
        assert(little_endian(bytes, local + 14U, 4) == crc);
        const std::size_t data =
            local + 30U + little_endian(bytes, local + 26U, 2) + little_endian(bytes, local + 28U, 2);
        assert(data + compressed <= bytes.size());

        ZipEntry entry;
        entry.mode = little_endian(bytes, offset + 38U, 4) >> 16;
        if (method == 0U) {
            entry.contents = bytes.substr(data, compressed);
        } else {
            assert(method == 8U);
            entry.contents = Inflater(reinterpret_cast<const std::uint8_t *>(bytes.data()) + data, compressed).run();
        }
        assert(entry.contents.size() == uncompressed);
        const auto *contents = reinterpret_cast<const std::uint8_t *>(entry.contents.data());
        assert(files_archive::crc32(0, contents, entry.contents.size()) == crc);
        entries.emplace(name, std::move(entry));
        offset += 46U + name_length + little_endian(bytes, offset + 30U, 2) + little_endian(bytes, offset + 32U, 2);
    }
    return entries;
}

void test_deflate_round_trip()
{
    std::mt19937 random(7);
    std::string mixed;
    for (int line = 0; line < 4000; ++line) mixed += "LILYGO archive line " + std::to_string(line % 97) + "\n";
    for (int index = 0; index < 70000; ++index) mixed.push_back(static_cast<char>(random() & 0xffU));
    mixed.append(100000, 'a');

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{4096}, mixed.size()}) {
        files_archive::DeflateEncoder encoder;
        std::vector<std::uint8_t> compressed;
        for (std::size_t offset = 0; offset < mixed.size(); offset += chunk) {
            const std::size_t length = std::min(chunk, mixed.size() - offset);
            encoder.write(reinterpret_cast<const std::uint8_t *>(mixed.data()) + offset, length);
            compressed.insert(compressed.end(), encoder.output().begin(), encoder.output().end());
            encoder.output().clear();
        }
        encoder.finish();
        compressed.insert(compressed.end(), encoder.output().begin(), encoder.output().end());
        assert(compressed.size() < mixed.size());
        assert(Inflater(compressed.data(), compressed.size()).run() == mixed);
    }

    files_archive::DeflateEncoder empty;
    empty.finish();
    assert(Inflater(empty.output().data(), empty.output().size()).run().empty());
    const std::string check = "123456789";
    assert(files_archive::crc32(0, reinterpret_cast<const std::uint8_t *>(check.data()), check.size()) == 0xcbf43926U);
}

void test_item_actions()
{
    TemporaryDirectory fixture("actions");
    TemporaryDirectory outside("actions-outside");
    fixture.write_file("Projects/plan.txt", "plan\n");
    fixture.write_file("Projects/.hidden", "hidden\n");
    fixture.write_file("Projects/Deep/notes.md", "deep notes\n");
    fixture.write_file("report.txt", "report contents\n");
    outside.write_file("kept.txt", "outside\n");
    assert(::symlink(outside.root().c_str(), fixture.path("outside-link").c_str()) == 0);
    set_modified(fixture.path("report.txt"), 1767225600);

    FilesModel model(fixture.root().string());
    assert(model.initialized());
    assert(!model.showActions(FilesModel::maximum_entries).succeeded());
    assert(model.screen() == FilesScreen::Home);

    // Get Info reports folder totals for visible items only, and dates of the item itself.
    assert(model.showActions(find_entry(model, "Projects")).succeeded());
    assert(model.screen() == FilesScreen::Actions);
    assert(model.actionEntry()->name == "Projects");
    assert(model.show(FilesScreen::Info).succeeded());
    const FilesItemInfo *info = model.info();
    assert(info != nullptr && info->is_directory && info->complete);
    assert(info->item_count == std::optional<std::size_t>(3U));
    assert(info->size == 16U);
    assert(info->location == "Files");
    assert(info->modified_time > 0);
    assert(model.back().succeeded());
    assert(model.info() == nullptr && model.actionEntry() == nullptr);

    assert(model.showActions(find_entry(model, "report.txt")).succeeded());
    assert(model.show(FilesScreen::Info).succeeded());
    assert(model.info()->size == 16U && !model.info()->item_count);
    assert(model.info()->modified_time == 1767225600);
    assert(model.back().succeeded());

    // Items in a subfolder name their location from the root.
    assert(model.openEntry(find_entry(model, "Projects")).succeeded());
    assert(model.showActions(find_entry(model, "plan.txt")).succeeded());
    assert(model.show(FilesScreen::Info).succeeded());
    assert(model.info()->location == "Files/Projects");
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Folder);
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);

    // Duplicate numbers the copy, keeps the date and copies folders with their hidden contents.
    assert(model.showActions(find_entry(model, "report.txt")).succeeded());
    assert(model.duplicateActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(read_file(fixture.path("report 2.txt")) == "report contents\n");
    assert(model.entry(find_entry(model, "report 2.txt"))->modified_time == 1767225600);
    assert(model.showActions(find_entry(model, "Projects")).succeeded());
    assert(model.duplicateActionItems().succeeded());
    assert(read_file(fixture.path("Projects 2/Deep/notes.md")) == "deep notes\n");
    assert(read_file(fixture.path("Projects 2/.hidden")) == "hidden\n");

    // Symbolic links are copied and deleted as links; their targets are never touched.
    assert(model.showActions(find_entry(model, "outside-link")).succeeded());
    assert(model.duplicateActionItems().succeeded());
    assert(std::filesystem::is_symlink(fixture.path("outside-link 2")));
    assert(model.showActions(find_entry(model, "outside-link 2")).succeeded());
    assert(model.removeActionItems().succeeded());
    assert(!std::filesystem::exists(fixture.path("outside-link 2")));
    assert(read_file(outside.path("kept.txt")) == "outside\n");

    // Rename validates names and refuses to replace another item.
    assert(model.showActions(find_entry(model, "report 2.txt")).succeeded());
    assert(model.show(FilesScreen::Rename).succeeded());
    assert(model.renameName() == "report 2.txt");
    for (const char *invalid : {"", ".", "..", "a/b"}) {
        model.setRenameName(invalid);
        const FilesCommandResult refused = model.renameActionEntry();
        assert(!refused.succeeded() && refused.status() == FilesCommandStatus::InvalidArgument);
        assert(model.screen() == FilesScreen::Rename);
    }
    model.setRenameName("report.txt");
    const FilesCommandResult taken = model.renameActionEntry();
    assert(!taken.succeeded() && taken.message().find("already taken") != std::string::npos);
    assert(read_file(fixture.path("report.txt")) == "report contents\n");
    model.setRenameName("summary.txt");
    assert(model.renameActionEntry().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(std::filesystem::exists(fixture.path("summary.txt")));
    assert(!std::filesystem::exists(fixture.path("report 2.txt")));

    // Copy and Paste: a folder cannot be pasted into itself; pasting next to the original numbers it.
    assert(!model.paste().succeeded());
    assert(model.showActions(find_entry(model, "Projects")).succeeded());
    assert(model.copyActionItems().succeeded());
    assert(model.hasClipboard() && model.clipboardCount() == 1U);
    assert(model.actionCapability(FilesAction::Paste).enabled);
    assert(model.openEntry(find_entry(model, "Projects")).succeeded());
    const FilesCommandResult into_itself = model.paste();
    assert(!into_itself.succeeded() && into_itself.status() == FilesCommandStatus::InvalidArgument);
    assert(model.back().succeeded());
    assert(model.paste().succeeded());
    assert(read_file(fixture.path("Projects 3/plan.txt")) == "plan\n");

    // Move: browse to a destination; the item's own folder and the folder itself are not destinations.
    assert(model.showActions(find_entry(model, "summary.txt")).succeeded());
    assert(model.show(FilesScreen::Move).succeeded());
    assert(model.moveAtRoot() && model.moveDisplayPath() == "~/" && model.moveDirectoryName() == "Files");
    assert(!model.canMoveHere());
    assert(!model.moveActionItems().succeeded());
    assert(model.moveDestinationCount() == 3U);
    assert(model.moveDestination(0)->name == "Projects");
    assert(model.openMoveDestination(0).succeeded());
    assert(model.moveDisplayPath() == "Projects" && model.moveParentName() == "Files");
    assert(model.moveDestinationCount() == 1U && model.moveDestination(0)->name == "Deep");
    assert(model.openMoveDestination(0).succeeded());
    assert(model.moveDisplayPath() == "Projects/Deep" && model.moveParentName() == "Projects");
    assert(model.moveDestinationUp().succeeded());
    assert(model.moveDisplayPath() == "Projects");
    assert(model.canMoveHere());
    assert(model.moveActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(std::filesystem::exists(fixture.path("Projects/summary.txt")));
    assert(!std::filesystem::exists(fixture.path("summary.txt")));

    assert(model.showActions(find_entry(model, "Projects")).succeeded());
    assert(model.show(FilesScreen::Move).succeeded());
    assert(!model.moveDestinationEnabled(0));
    assert(!model.openMoveDestination(0).succeeded());
    assert(model.openMoveDestination(1).succeeded());
    assert(model.moveDisplayPath() == "Projects 2");
    assert(model.moveActionItems().succeeded());
    assert(std::filesystem::exists(fixture.path("Projects 2/Projects/summary.txt")));

    // A name that already exists at the destination is refused.
    assert(model.showActions(find_entry(model, "report.txt")).succeeded());
    fixture.write_file("Projects 3/report.txt", "other\n");
    assert(model.show(FilesScreen::Move).succeeded());
    std::size_t destination = 0;
    while (destination < model.moveDestinationCount() && model.moveDestination(destination)->name != "Projects 3") {
        ++destination;
    }
    assert(model.openMoveDestination(destination).succeeded());
    assert(model.moveDisplayPath() == "Projects 3");
    const FilesCommandResult collision = model.moveActionItems();
    assert(!collision.succeeded() && collision.message().find("already exists") != std::string::npos);
    assert(model.screen() == FilesScreen::Move);
    assert(read_file(fixture.path("Projects 3/report.txt")) == "other\n");
    assert(model.back().succeeded());

    // New Folder with 1 Item moves the item into a new folder and offers to rename it.
    fixture.make_directory("New Folder");
    assert(model.showActions(find_entry(model, "report.txt")).succeeded());
    assert(model.newFolderWithActionItems().succeeded());
    assert(model.screen() == FilesScreen::Rename);
    assert(model.renameName() == "New Folder 2");
    assert(model.actionEntry() != nullptr && model.actionEntry()->name == "New Folder 2");
    assert(std::filesystem::exists(fixture.path("New Folder 2/report.txt")));
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);

    // Delete removes folders with everything in them.
    assert(model.showActions(find_entry(model, "Projects 2")).succeeded());
    assert(model.show(FilesScreen::ConfirmDelete).succeeded());
    assert(model.removeActionItems().succeeded());
    assert(!std::filesystem::exists(fixture.path("Projects 2")));
    assert(model.screen() == FilesScreen::Home);

    // Pasting a deleted item fails and empties the clipboard.
    assert(model.showActions(find_entry(model, "Projects 3")).succeeded());
    assert(model.copyActionItems().succeeded());
    std::filesystem::remove_all(fixture.path("Projects 3"));
    assert(!model.paste().succeeded());
    assert(!model.hasClipboard());
}

void test_compress_archive()
{
    TemporaryDirectory fixture("compress");
    std::string text;
    for (int line = 0; line < 3000; ++line) text += "Compressible line " + std::to_string(line % 50) + "\n";
    fixture.write_file("Bundle/text.txt", text);
    fixture.write_file("Bundle/Inner/empty.txt", "");
    fixture.write_file("Bundle/.dotfile", "dot\n");
    std::mt19937 random(3);
    std::string noise;
    for (int index = 0; index < 5000; ++index) noise.push_back(static_cast<char>(random() & 0xffU));
    fixture.write_file("Bundle/noise.bin", noise);
    assert(::symlink("text.txt", fixture.path("Bundle/link").c_str()) == 0);
    fixture.write_file("single.txt", text);

    FilesModel model(fixture.root().string());
    assert(model.showActions(find_entry(model, "Bundle")).succeeded());
    assert(model.compressActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home);
    find_entry(model, "Bundle.zip");

    const auto entries = read_zip(fixture.path("Bundle.zip"));
    assert(entries.size() == 7U);
    assert(entries.at("Bundle/").contents.empty() && S_ISDIR(entries.at("Bundle/").mode));
    assert(entries.at("Bundle/Inner/").contents.empty());
    assert(entries.at("Bundle/text.txt").contents == text);
    assert(entries.at("Bundle/Inner/empty.txt").contents.empty());
    assert(entries.at("Bundle/.dotfile").contents == "dot\n");
    assert(entries.at("Bundle/noise.bin").contents == noise);
    assert(entries.at("Bundle/link").contents == "text.txt" && S_ISLNK(entries.at("Bundle/link").mode));
    assert(std::filesystem::file_size(fixture.path("Bundle.zip")) < text.size());

    // Compressing again keeps the first archive and adds a numbered one; single files keep their extension.
    assert(model.showActions(find_entry(model, "Bundle")).succeeded());
    assert(model.compressActionItems().succeeded());
    assert(std::filesystem::exists(fixture.path("Bundle 2.zip")));
    assert(model.showActions(find_entry(model, "single.txt")).succeeded());
    assert(model.compressActionItems().succeeded());
    assert(read_zip(fixture.path("single.txt.zip")).at("single.txt").contents == text);
}

void select_entries(FilesModel &model, std::initializer_list<std::string_view> names)
{
    assert(model.show(FilesScreen::Select).succeeded());
    for (std::string_view name : names) assert(model.toggleSelection(find_entry(model, name)));
    assert(model.selectedCount() == names.size());
}

void test_selection_actions()
{
    TemporaryDirectory fixture("selection");
    fixture.write_file("Projects/plan.txt", "plan\n");
    fixture.write_file("Projects/Deep/notes.md", "deep notes\n");
    fixture.make_directory("Other");
    fixture.write_file("a.txt", "alpha\n");
    fixture.write_file("b.txt", "beta\n");
    fixture.write_file("c.md", "gamma\n");
    set_modified(fixture.path("a.txt"), 1767225600);

    FilesModel model(fixture.root().string());
    assert(model.initialized());

    // Without a selection the toolbar commands fail and selection mode stays as it was.
    assert(model.show(FilesScreen::Select).succeeded());
    assert(!model.actionCapability(FilesAction::Duplicate).enabled);
    for (const FilesScreen screen : {FilesScreen::Move, FilesScreen::ConfirmDelete, FilesScreen::SelectionActions}) {
        const FilesCommandResult refused = model.show(screen);
        assert(!refused.succeeded() && refused.status() == FilesCommandStatus::InvalidArgument);
        assert(model.screen() == FilesScreen::Select && model.actionCount() == 0U);
    }
    assert(!model.duplicateActionItems().succeeded());
    assert(model.screen() == FilesScreen::Select && model.selecting());
    assert(model.back().succeeded());
    assert(!model.selecting());

    // Duplicate numbers each copy, keeps dates and leaves selection mode.
    select_entries(model, {"a.txt", "Projects"});
    assert(model.actionCapability(FilesAction::Duplicate).enabled);
    assert(!model.actionCapability(FilesAction::GetInfo).enabled);
    assert(model.duplicateActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home && !model.selecting() && model.selectedCount() == 0U);
    assert(read_file(fixture.path("a 2.txt")) == "alpha\n");
    assert(model.entry(find_entry(model, "a 2.txt"))->modified_time == 1767225600);
    assert(read_file(fixture.path("Projects 2/Deep/notes.md")) == "deep notes\n");

    // The selection menu takes the selected items; closing it keeps them selected.
    select_entries(model, {"a.txt", "b.txt"});
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.screen() == FilesScreen::SelectionActions && model.selecting());
    assert(model.actionCount() == 2U && model.actionEntry() == nullptr);
    assert(model.actionCapability(FilesAction::Copy).enabled && !model.actionCapability(FilesAction::Rename).enabled);
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Select && model.actionCount() == 0U);
    assert(model.isSelected(find_entry(model, "a.txt")) && model.isSelected(find_entry(model, "b.txt")));

    // Copy puts every item on the clipboard; Paste adds them all, numbering names that are taken.
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.copyActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home && model.selectedCount() == 0U);
    assert(model.clipboardCount() == 2U);
    assert(model.openEntry(find_entry(model, "Other")).succeeded());
    assert(model.paste().succeeded());
    assert(read_file(fixture.path("Other/a.txt")) == "alpha\n" && read_file(fixture.path("Other/b.txt")) == "beta\n");
    assert(model.back().succeeded());
    assert(model.paste().succeeded());
    assert(std::filesystem::exists(fixture.path("a 3.txt")) && std::filesystem::exists(fixture.path("b 2.txt")));

    // Nothing is pasted when one of the copied folders would go into itself.
    select_entries(model, {"c.md", "Projects"});
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.copyActionItems().succeeded());
    assert(model.openEntry(find_entry(model, "Projects")).succeeded());
    const FilesCommandResult into_itself = model.paste();
    assert(!into_itself.succeeded() && into_itself.status() == FilesCommandStatus::InvalidArgument);
    assert(!std::filesystem::exists(fixture.path("Projects/c.md")));
    assert(model.back().succeeded());

    // Several items are compressed into Archive.zip, one item under its own name.
    select_entries(model, {"a.txt", "c.md", "Projects"});
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.compressActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home);
    const auto archive = read_zip(fixture.path("Archive.zip"));
    assert(archive.size() == 6U);
    assert(archive.at("a.txt").contents == "alpha\n" && archive.at("c.md").contents == "gamma\n");
    assert(archive.at("Projects/").contents.empty() && archive.at("Projects/Deep/").contents.empty());
    assert(archive.at("Projects/plan.txt").contents == "plan\n");
    assert(archive.at("Projects/Deep/notes.md").contents == "deep notes\n");
    select_entries(model, {"a.txt", "c.md"});
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.compressActionItems().succeeded());
    assert(read_zip(fixture.path("Archive 2.zip")).size() == 2U);
    select_entries(model, {"c.md"});
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.compressActionItems().succeeded());
    assert(read_zip(fixture.path("c.md.zip")).at("c.md").contents == "gamma\n");

    // New Folder with the items moves them all into one new folder, ready to be renamed.
    select_entries(model, {"Archive.zip", "Archive 2.zip", "c.md.zip"});
    assert(model.show(FilesScreen::SelectionActions).succeeded());
    assert(model.newFolderWithActionItems().succeeded());
    assert(model.screen() == FilesScreen::Rename && !model.selecting());
    assert(model.renameName() == "New Folder" && model.actionEntry()->name == "New Folder");
    for (const char *name : {"Archive.zip", "Archive 2.zip", "c.md.zip"}) {
        assert(std::filesystem::exists(fixture.path("New Folder") / name));
        assert(!std::filesystem::exists(fixture.path(name)));
    }
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Home);

    // Move: a selected folder is no destination and nothing moves when one name is taken there.
    select_entries(model, {"c.md", "Projects"});
    assert(model.show(FilesScreen::Move).succeeded());
    assert(model.selecting() && model.actionCount() == 2U && !model.canMoveHere());
    std::size_t projects = 0;
    while (model.moveDestination(projects)->name != "Projects") ++projects;
    assert(!model.moveDestinationEnabled(projects));
    assert(!model.openMoveDestination(projects).succeeded());
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Select && model.selectedCount() == 2U);
    assert(model.show(FilesScreen::Move).succeeded());
    std::size_t other = 0;
    while (model.moveDestination(other)->name != "Other") ++other;
    assert(model.openMoveDestination(other).succeeded());
    assert(model.canMoveHere());
    fixture.write_file("Other/c.md", "taken\n");
    const FilesCommandResult taken = model.moveActionItems();
    assert(!taken.succeeded() && taken.message().find("already exists") != std::string::npos);
    assert(model.screen() == FilesScreen::Move);
    assert(std::filesystem::exists(fixture.path("Projects")) && read_file(fixture.path("c.md")) == "gamma\n");
    std::filesystem::remove(fixture.path("Other/c.md"));
    assert(model.moveActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home && !model.selecting());
    assert(read_file(fixture.path("Other/c.md")) == "gamma\n");
    assert(read_file(fixture.path("Other/Projects/plan.txt")) == "plan\n");

    // Delete asks about every selected item; cancelling keeps the selection.
    select_entries(model, {"a 2.txt", "b 2.txt"});
    assert(model.show(FilesScreen::ConfirmDelete).succeeded());
    assert(model.actionCount() == 2U);
    assert(model.back().succeeded());
    assert(model.screen() == FilesScreen::Select && model.selectedCount() == 2U);
    assert(model.show(FilesScreen::ConfirmDelete).succeeded());
    assert(model.removeActionItems().succeeded());
    assert(model.screen() == FilesScreen::Home);
    assert(!std::filesystem::exists(fixture.path("a 2.txt")) && !std::filesystem::exists(fixture.path("b 2.txt")));

    // A command that fails part way shows what changed and keeps the remaining items selected.
    select_entries(model, {"a.txt", "b.txt"});
    std::filesystem::remove(fixture.path("b.txt"));
    const FilesCommandResult missing = model.duplicateActionItems();
    assert(!missing.succeeded() && missing.message().find("Unable to duplicate") != std::string::npos);
    assert(model.screen() == FilesScreen::Select && model.error() == missing.message());
    assert(std::filesystem::exists(fixture.path("a 2.txt")));
    assert(model.selectedCount() == 1U && model.isSelected(find_entry(model, "a.txt")));
    assert(model.back().succeeded());

    if (::geteuid() != 0) {
        // Only the folder that cannot be emptied is left to delete, still selected beneath the prompt.
        fixture.write_file("Locked/kept.txt", "kept\n");
        fixture.make_directory("Alpha");
        assert(model.openEntry(find_entry(model, "Other")).succeeded() && model.back().succeeded());
        select_entries(model, {"Alpha", "Locked", "a.txt"});
        assert(::chmod(fixture.path("Locked").c_str(), 0555) == 0);
        assert(model.show(FilesScreen::ConfirmDelete).succeeded());
        const FilesCommandResult locked = model.removeActionItems();
        assert(::chmod(fixture.path("Locked").c_str(), 0755) == 0);
        assert(!locked.succeeded() && locked.message().find("Unable to delete") != std::string::npos);
        assert(model.screen() == FilesScreen::ConfirmDelete && model.actionCount() == 2U);
        assert(!std::filesystem::exists(fixture.path("Alpha")));
        assert(model.back().succeeded());
        assert(model.screen() == FilesScreen::Select && model.selectedCount() == 2U);
        assert(model.isSelected(find_entry(model, "Locked")) && model.isSelected(find_entry(model, "a.txt")));
    }
}

void test_config_read_value()
{
    using lilygo::ui::files::config::read_value;
    using lilygo::ui::files::config::ReadStatus;

    TemporaryDirectory fixture("config");
    fixture.write_file("application.conf",
                       "# application settings\n"
                       " ignored = value \n"
                       " files.root =   /home/pi/files   \n");
    const auto found = read_value(fixture.path("application.conf").string(), "files.root");
    assert(found.status == ReadStatus::Found);
    assert(found.value == "/home/pi/files");

    assert(read_value(fixture.path("application.conf").string(), "missing").status == ReadStatus::NotFound);
    assert(read_value(fixture.path("application.conf").string(), "files.root", 4).status == ReadStatus::Invalid);
    assert(read_value({}, "files.root").status == ReadStatus::Invalid);
    assert(read_value(fixture.path("missing.conf").string(), "files.root").status == ReadStatus::NotFound);

    fixture.write_file("too-long.conf", std::string(4096, 'x') + "\n");
    assert(read_value(fixture.path("too-long.conf").string(), "files.root").status == ReadStatus::Invalid);
}

}  // namespace

int main()
{
    test_file_browsing_and_boundaries();
    test_search_creation_and_bounded_text();
    test_large_preview_is_truncated();
    test_sorting_and_view_mode();
    test_selection_mode();
    test_deflate_round_trip();
    test_item_actions();
    test_compress_archive();
    test_selection_actions();
    test_config_read_value();
    return 0;
}
