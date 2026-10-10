#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_MODEL_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_MODEL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
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
    Actions,
    Info,
    Rename,
    ConfirmDelete,
    SelectionActions,
};

enum class FilesEntryKind : std::int32_t {
    Folder,
    Music,
    Pdf,
    Text,
    Binary,
};

enum class FilesSortKey : std::int32_t {
    Name,
    Kind,
    Date,
    Size,
};

enum class FilesViewMode : std::int32_t {
    Icons,
    List,
};

struct FilesEntry {
    std::string name;
    FilesEntryKind kind        = FilesEntryKind::Binary;
    std::uint64_t size         = 0;
    std::int64_t modified_time = 0;
    std::optional<std::size_t> item_count;
    bool is_directory = false;
    bool is_symlink   = false;
};

// Details shown by Get Info. Folder sizes and item counts cover their visible contents recursively; the
// location names the containing folder from "Files", the root.
struct FilesItemInfo {
    std::string name;
    std::string location;
    FilesEntryKind kind     = FilesEntryKind::Binary;
    bool is_directory       = false;
    bool is_symlink         = false;
    std::uint64_t size      = 0;
    std::uint64_t disk_size = 0;
    std::optional<std::size_t> item_count;
    bool complete = true;
    std::optional<std::int64_t> created_time;
    std::int64_t modified_time = 0;
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
    Compress,
    Remove,
    ChangeSort,
    ChangeView,
    Duplicate,
    GetInfo,
    NewFolderWithItem,
    Paste,
    Count,
};

struct FilesActionCapability {
    bool supported = false;
    bool enabled   = false;
    std::string_view reason;
};

class FilesModel {
public:
    static constexpr std::size_t text_capacity             = 64;
    static constexpr std::size_t name_capacity             = 256;
    static constexpr std::size_t detail_capacity           = 48;
    static constexpr std::size_t path_capacity             = 4096;
    static constexpr std::size_t maximum_entries           = 128;
    static constexpr std::size_t preview_capacity          = 8192;
    static constexpr std::size_t maximum_info_items        = 10000;
    static constexpr std::string_view default_folder_name  = "New Folder";
    static constexpr std::string_view default_archive_name = "Archive";

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
    [[nodiscard]] FilesSortKey sortKey() const noexcept;
    [[nodiscard]] bool sortAscending() const noexcept;
    [[nodiscard]] FilesViewMode viewMode() const noexcept;

    [[nodiscard]] std::size_t entryCount() const noexcept;
    [[nodiscard]] const FilesEntry *entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t searchCount() const noexcept;
    [[nodiscard]] const FilesEntry *searchEntry(std::size_t index) const noexcept;
    [[nodiscard]] const FilesEntry *previewEntry() const noexcept;

    // The action items are what the context menu, the selection menu, Move and Delete act on: the item a
    // long press chose, or the items selected when the selection toolbar opened one of them. Get Info and
    // Rename act on a single action item, which actionEntry() returns.
    [[nodiscard]] const FilesEntry *actionEntry() const noexcept;
    [[nodiscard]] std::size_t actionIndex() const noexcept;
    [[nodiscard]] std::size_t actionCount() const noexcept;
    // Whether selection mode is open, alone or beneath a menu, sheet or prompt acting on the selected items.
    [[nodiscard]] bool selecting() const noexcept;
    [[nodiscard]] const std::string &renameName() const noexcept;
    [[nodiscard]] const FilesItemInfo *info() const noexcept;
    [[nodiscard]] bool hasClipboard() const noexcept;
    [[nodiscard]] std::size_t clipboardCount() const noexcept;

    // The folder browser that picks where Move puts the action items.
    [[nodiscard]] const std::string &moveDisplayPath() const noexcept;
    [[nodiscard]] std::string moveDirectoryName() const;
    [[nodiscard]] std::string moveParentName() const;
    [[nodiscard]] bool moveAtRoot() const noexcept;
    [[nodiscard]] std::size_t moveDestinationCount() const noexcept;
    [[nodiscard]] const FilesEntry *moveDestination(std::size_t index) const noexcept;
    [[nodiscard]] bool moveDestinationEnabled(std::size_t index) const noexcept;
    [[nodiscard]] bool canMoveHere() const;

    [[nodiscard]] bool isSelected(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t selectedCount() const noexcept;

    FilesCommandResult show(FilesScreen screen);
    FilesCommandResult back();
    void setQuery(std::string_view query);
    void setFolderName(std::string_view name);
    void setRenameName(std::string_view name);
    FilesCommandResult openEntry(std::size_t index);
    FilesCommandResult openSearchEntry(std::size_t index);
    FilesCommandResult activateEntry(std::size_t index);
    FilesCommandResult createFolder();
    FilesCommandResult setSortKey(FilesSortKey key);
    FilesCommandResult setViewMode(FilesViewMode mode);
    bool toggleSelection(std::size_t index) noexcept;
    void selectAll() noexcept;
    void clearSelection() noexcept;
    void toggleSelectAll() noexcept;

    // In selection mode, show() takes the selected items as the action items of Move, ConfirmDelete and
    // SelectionActions; cancelling those returns to selection mode with the items still selected.
    FilesCommandResult showActions(std::size_t index);
    FilesCommandResult openActionEntry();
    FilesCommandResult renameActionEntry();
    FilesCommandResult paste();
    // These act on every action item, or on the selected items in selection mode. Success leaves selection
    // mode; a failure stays on the current screen, and items still there stay chosen.
    FilesCommandResult copyActionItems();
    FilesCommandResult compressActionItems();
    FilesCommandResult duplicateActionItems();
    FilesCommandResult newFolderWithActionItems();
    FilesCommandResult removeActionItems();
    FilesCommandResult openMoveDestination(std::size_t index);
    FilesCommandResult moveDestinationUp();
    FilesCommandResult moveActionItems();

    [[nodiscard]] FilesActionCapability actionCapability(FilesAction action) const noexcept;
    [[nodiscard]] std::uint32_t supportedActionMask() const noexcept;
    [[nodiscard]] std::uint32_t enabledActionMask() const noexcept;

private:
    struct ActionItem {
        std::string name;
        std::string path;
        bool is_directory = false;
    };

    FilesCommandResult refresh();
    FilesCommandResult enterDirectory(std::size_t index);
    FilesCommandResult goToParent();
    FilesCommandResult readPreview(std::size_t index);
    FilesCommandResult loadInfo(const FilesEntry &target);
    FilesCommandResult loadMoveDestinations(const std::string &path);
    FilesCommandResult prepareItemScreen(FilesScreen screen);
    FilesCommandResult requireActionEntry(const FilesEntry *&target, std::string &path);
    FilesCommandResult requireActionItems(std::vector<ActionItem> &items);
    FilesCommandResult finishOperation();
    FilesCommandResult failAfter(std::size_t changed, const FilesCommandResult &failure);
    FilesCommandResult fail(FilesCommandStatus status, std::string message);
    FilesCommandResult systemError(std::string_view operation, int error_number);
    void returnToContent() noexcept;
    void returnToSelection() noexcept;
    void keepRemainingItems();
    void retargetClipboard(const std::string &from, const std::string &to);
    void sortEntries();
    void updateContentScreen() noexcept;
    void resetSelection();
    void reselect(const std::vector<std::string> &names);
    void clearError() noexcept;
    void setError(std::string message);

    [[nodiscard]] std::size_t findEntry(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> selectedNames() const;
    [[nodiscard]] std::string displayPathOf(const std::string &path) const;
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
    std::string rename_name_;
    std::vector<std::string> action_names_;
    // The action items came from selection mode, which cancelling returns to.
    bool selection_action_ = false;
    std::vector<std::string> clipboard_paths_;
    std::string move_path_;
    std::string move_display_path_;
    std::vector<FilesEntry> move_entries_;
    FilesItemInfo info_;
    bool has_info_ = false;
    std::string preview_;
    std::string error_;
    std::vector<FilesEntry> entries_;
    std::vector<bool> selected_;
    std::size_t preview_index_ = maximum_entries;
    FilesSortKey sort_key_     = FilesSortKey::Name;
    bool sort_ascending_       = true;
    FilesViewMode view_mode_   = FilesViewMode::Icons;
};

#endif
