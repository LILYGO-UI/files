#ifndef LILYGO_UI_FILES_TESTS_TEST_FIXTURE_HPP
#define LILYGO_UI_FILES_TESTS_TEST_FIXTURE_HPP

#include "pages/files/files_model.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <cstdlib>
#include <unistd.h>

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view purpose = "fixture")
    {
        std::string pattern = "/tmp/lilygo-ui-files-";
        pattern.append(purpose);
        pattern += "-XXXXXX";
        storage_.assign(pattern.begin(), pattern.end());
        storage_.push_back('\0');

        char *created = ::mkdtemp(storage_.data());
        assert(created != nullptr);
        root_ = created;
        assert(root_.string().rfind("/tmp/lilygo-ui-files-", 0) == 0);
    }

    ~TemporaryDirectory()
    {
        if (root_.empty() || root_.string().rfind("/tmp/lilygo-ui-files-", 0) != 0) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        assert(!error);
    }

    TemporaryDirectory(const TemporaryDirectory &)            = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const noexcept
    {
        return root_;
    }

    [[nodiscard]] std::filesystem::path path(std::string_view relative) const
    {
        return root_ / std::filesystem::path(relative);
    }

    void make_directory(std::string_view relative) const
    {
        std::error_code error;
        const bool created = std::filesystem::create_directories(path(relative), error);
        assert(!error);
        assert(created || std::filesystem::is_directory(path(relative)));
    }

    void write_file(std::string_view relative, std::string_view contents) const
    {
        const auto destination = path(relative);
        if (destination.has_parent_path()) {
            std::error_code error;
            std::filesystem::create_directories(destination.parent_path(), error);
            assert(!error);
        }

        std::ofstream output(destination, std::ios::binary);
        assert(output);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        assert(output);
    }

private:
    std::vector<char> storage_;
    std::filesystem::path root_;
};

inline std::size_t find_entry(const FilesModel &model, std::string_view name)
{
    for (std::size_t index = 0; index < model.entryCount(); ++index) {
        const FilesEntry *entry = model.entry(index);
        if (entry != nullptr && entry->name == name) return index;
    }
    assert(false && "fixture entry was not found");
    return FilesModel::maximum_entries;
}

#endif
