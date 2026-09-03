#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_MODEL_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class FilesScreen : std::int32_t {
    Home,
    Folder,
    Search,
    Select,
    Preview,
    NewFolder,
    Move,
    More,
};

enum class FilesEntryKind : std::int32_t {
    Folder,
    Download,
    Music,
    Pdf,
    Text,
    Binary,
};

struct FilesEntry {
    std::string name;
    FilesEntryKind kind = FilesEntryKind::Binary;
    std::uint64_t size  = 0;
    bool is_directory   = false;
    bool is_symlink     = false;
};

enum class FilesCommandStatus : std::int32_t {
    Success,
    InvalidArgument,
    NotFound,
    IoError,
    Unsupported,
};

class FilesCommandResult {
public:
    static FilesCommandResult success();
    static FilesCommandResult failure(FilesCommandStatus status, std::string message);

    [[nodiscard]] bool succeeded() const noexcept;
    [[nodiscard]] FilesCommandStatus status() const noexcept;
    [[nodiscard]] const std::string &message() const noexcept;

private:
    FilesCommandResult(FilesCommandStatus status, std::string message);

    FilesCommandStatus status_ = FilesCommandStatus::Success;
    std::string message_;
};

enum class FilesAction : std::int32_t {
    Copy,
    Rename,
    Move,
    Archive,
    Remove,
    ChangeSort,
    ChangeView,
    Count,
};

struct FilesActionCapability {
    bool supported = false;
    bool enabled   = false;
    std::string_view reason;
};

class FilesModel {
public:
    static constexpr std::size_t text_capacity            = 64;
    static constexpr std::size_t name_capacity            = 256;
    static constexpr std::size_t detail_capacity          = 48;
    static constexpr std::size_t path_capacity            = 4096;
    static constexpr std::size_t maximum_entries          = 128;
    static constexpr std::size_t preview_capacity         = 8192;
    static constexpr std::string_view default_folder_name = "New Folder";

    FilesModel();
    explicit FilesModel(std::string_view root_path);

    FilesCommandResult initialize(std::string_view root_path);

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] FilesScreen screen() const noexcept;
    [[nodiscard]] FilesScreen contentScreen() const noexcept;
    [[nodiscard]] const std::string &query() const noexcept;
    [[nodiscard]] const std::string &folderName() const noexcept;
    [[nodiscard]] const std::string &preview() const noexcept;
    [[nodiscard]] const std::string &error() const noexcept;
    [[nodiscard]] const std::string &displayPath() const noexcept;
    [[nodiscard]] std::string directoryName() const;
    [[nodiscard]] std::uint64_t capacityBytes() const noexcept;
    [[nodiscard]] std::uint64_t usedBytes() const noexcept;

    [[nodiscard]] std::size_t entryCount() const noexcept;
    [[nodiscard]] const FilesEntry *entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t searchCount() const noexcept;
    [[nodiscard]] const FilesEntry *searchEntry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t destinationCount() const noexcept;
    [[nodiscard]] const FilesEntry *destinationEntry(std::size_t index) const noexcept;
    [[nodiscard]] const FilesEntry *previewEntry() const noexcept;

    [[nodiscard]] bool isSelected(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t selectedCount() const noexcept;

    FilesCommandResult show(FilesScreen screen);
    FilesCommandResult back();
    void setQuery(std::string_view query);
    void setFolderName(std::string_view name);
    FilesCommandResult openEntry(std::size_t index);
    FilesCommandResult openSearchEntry(std::size_t index);
    FilesCommandResult createFolder();
    bool toggleSelection(std::size_t index) noexcept;
    void selectAll() noexcept;
    void clearSelection() noexcept;

    [[nodiscard]] FilesActionCapability actionCapability(FilesAction action) const noexcept;
    [[nodiscard]] std::uint32_t supportedActionMask() const noexcept;
    [[nodiscard]] std::uint32_t enabledActionMask() const noexcept;

    FilesCommandResult copySelection() const;
    FilesCommandResult renameSelection(std::string_view new_name) const;
    FilesCommandResult moveSelectionTo(std::size_t destination_index) const;
    FilesCommandResult archiveSelection() const;
    FilesCommandResult removeSelection() const;
    FilesCommandResult changeSort() const;
    FilesCommandResult changeView() const;

private:
    FilesCommandResult refresh();
    FilesCommandResult enterDirectory(std::size_t index);
    FilesCommandResult goToParent();
    FilesCommandResult readPreview(std::size_t index);
    FilesCommandResult unsupported(FilesAction action) const;
    void updateContentScreen() noexcept;
    void resetSelection();
    void clearError() noexcept;
    void setError(std::string message);

    [[nodiscard]] static std::string boundedText(std::string_view value, std::size_t maximum_bytes);
    [[nodiscard]] static bool containsCaseInsensitive(std::string_view text, std::string_view query) noexcept;
    [[nodiscard]] static std::uint32_t actionBit(FilesAction action) noexcept;

    bool initialized_           = false;
    FilesScreen screen_         = FilesScreen::Home;
    FilesScreen content_screen_ = FilesScreen::Home;
    std::string root_path_;
    std::string current_path_;
    std::string display_path_ = "~/";
    std::string query_;
    std::string folder_name_{default_folder_name};
    std::string preview_;
    std::string error_;
    std::vector<FilesEntry> entries_;
    std::vector<bool> selected_;
    std::size_t preview_index_    = maximum_entries;
    std::uint64_t capacity_bytes_ = 0;
    std::uint64_t used_bytes_     = 0;
};

#endif
