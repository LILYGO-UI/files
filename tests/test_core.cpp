#include "app_config.hpp"
#include "pages/files/files_model.hpp"

#include "test_fixture.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

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
    assert(model.capacityBytes() >= model.usedBytes());

    assert(model.entry(0) != nullptr && model.entry(0)->is_directory);
    assert(model.entry(1) != nullptr && model.entry(1)->is_directory);
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
    test_config_read_value();
    return 0;
}
