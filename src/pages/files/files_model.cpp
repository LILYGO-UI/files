#include "pages/files/files_model.hpp"

#include "pages/files/files_archive.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {

constexpr int kMaximumTreeDepth = 64;

FilesCommandResult systemFailure(const char *operation, int error_number)
{
    std::string message(operation);
    message += ": ";
    message += std::strerror(error_number);
    return FilesCommandResult::failure(FilesCommandStatus::IoError, std::move(message));
}

bool isKnownScreen(FilesScreen screen) noexcept
{
    switch (screen) {
        case FilesScreen::Home:
        case FilesScreen::Folder:
        case FilesScreen::Search:
        case FilesScreen::Select:
        case FilesScreen::Preview:
        case FilesScreen::NewFolder:
        case FilesScreen::Move:
        case FilesScreen::More:
        case FilesScreen::Actions:
        case FilesScreen::Info:
        case FilesScreen::Rename:
        case FilesScreen::ConfirmDelete:
        case FilesScreen::SelectionActions:
            return true;
    }
    return false;
}

bool actsOnItem(FilesScreen screen) noexcept
{
    return screen == FilesScreen::Actions || screen == FilesScreen::Info || screen == FilesScreen::Rename ||
           screen == FilesScreen::Move || screen == FilesScreen::ConfirmDelete ||
           screen == FilesScreen::SelectionActions;
}

// Screens the selection toolbar opens for the selected items.
bool actsOnSelection(FilesScreen screen) noexcept
{
    return screen == FilesScreen::Move || screen == FilesScreen::ConfirmDelete ||
           screen == FilesScreen::SelectionActions;
}

bool joinPath(std::string &path, std::string_view directory, std::string_view name)
{
    const std::size_t separator = directory.empty() || directory.back() == '/' ? 0U : 1U;
    if (directory.size() + separator + name.size() >= FilesModel::path_capacity) {
        return false;
    }

    path.assign(directory);
    if (separator != 0U) path.push_back('/');
    path.append(name);
    return true;
}

std::string_view nameOf(std::string_view path)
{
    return path.substr(path.find_last_of('/') + 1U);
}

bool pathIsWithinRoot(std::string_view path, std::string_view root)
{
    if (path == root) return true;
    if (root == "/") return !path.empty() && path.front() == '/';
    return path.size() > root.size() && path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}

std::string_view extensionOf(std::string_view name)
{
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0U) return {};
    return name.substr(dot + 1U);
}

bool equalsCaseInsensitive(std::string_view left, std::string_view right)
{
    return left.size() == right.size() && strncasecmp(left.data(), right.data(), left.size()) == 0;
}

FilesEntryKind classifyEntry(std::string_view name, bool is_directory, bool is_symlink)
{
    if (is_directory) return FilesEntryKind::Folder;
    if (is_symlink) return FilesEntryKind::Binary;

    const std::string_view extension = extensionOf(name);
    if (equalsCaseInsensitive(extension, "pdf")) return FilesEntryKind::Pdf;
    if (equalsCaseInsensitive(extension, "txt") || equalsCaseInsensitive(extension, "md") ||
        equalsCaseInsensitive(extension, "log") || equalsCaseInsensitive(extension, "json") ||
        equalsCaseInsensitive(extension, "conf") || equalsCaseInsensitive(extension, "ini") ||
        equalsCaseInsensitive(extension, "c") || equalsCaseInsensitive(extension, "h") ||
        equalsCaseInsensitive(extension, "cpp") || equalsCaseInsensitive(extension, "hpp") ||
        equalsCaseInsensitive(extension, "py") || equalsCaseInsensitive(extension, "sh")) {
        return FilesEntryKind::Text;
    }
    if (equalsCaseInsensitive(extension, "mp3") || equalsCaseInsensitive(extension, "wav") ||
        equalsCaseInsensitive(extension, "flac") || equalsCaseInsensitive(extension, "ogg")) {
        return FilesEntryKind::Music;
    }
    return FilesEntryKind::Binary;
}

// Counts the entries a folder would list when opened; hidden entries are excluded the same way.
std::optional<std::size_t> countVisibleEntries(const std::string &path)
{
    DIR *directory = opendir(path.c_str());
    if (directory == nullptr) return std::nullopt;

    std::size_t count = 0;
    while (const dirent *item = readdir(directory)) {
        if (item->d_name[0] != '.') ++count;
    }
    closedir(directory);
    return count;
}

template <typename Value>
int compareValues(const Value &left, const Value &right)
{
    if (left < right) return -1;
    return right < left ? 1 : 0;
}

int compareCaseInsensitive(std::string_view left, std::string_view right)
{
    const int comparison = strncasecmp(left.data(), right.data(), std::min(left.size(), right.size()));
    return comparison != 0 ? comparison : compareValues(left.size(), right.size());
}

int compareNames(const FilesEntry &left, const FilesEntry &right)
{
    const int comparison = compareCaseInsensitive(left.name, right.name);
    return comparison != 0 ? comparison : left.name.compare(right.name);
}

int compareBy(FilesSortKey key, const FilesEntry &left, const FilesEntry &right)
{
    switch (key) {
        case FilesSortKey::Name:
            return compareNames(left, right);
        case FilesSortKey::Kind:
            return compareCaseInsensitive(extensionOf(left.name), extensionOf(right.name));
        case FilesSortKey::Date:
            return compareValues(left.modified_time, right.modified_time);
        case FilesSortKey::Size:
            // Folders are compared by the item count they show in place of a size.
            if (left.is_directory) return compareValues(left.item_count.value_or(0U), right.item_count.value_or(0U));
            return compareValues(left.size, right.size);
    }
    return 0;
}

bool isKnownSortKey(FilesSortKey key) noexcept
{
    switch (key) {
        case FilesSortKey::Name:
        case FilesSortKey::Kind:
        case FilesSortKey::Date:
        case FilesSortKey::Size:
            return true;
    }
    return false;
}

bool isKnownViewMode(FilesViewMode mode) noexcept
{
    return mode == FilesViewMode::Icons || mode == FilesViewMode::List;
}

// Names and kinds start at A; dates and sizes start with the newest and largest entries.
bool defaultAscending(FilesSortKey key) noexcept
{
    return key == FilesSortKey::Name || key == FilesSortKey::Kind;
}

// Names may not be empty, name the folder itself or its parent, or contain a path separator.
bool isValidName(std::string_view name) noexcept
{
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

// Splits "notes.txt" into "notes" and ".txt"; folders and names without an extension keep their whole name.
std::pair<std::string_view, std::string_view> splitExtension(std::string_view name, bool is_directory)
{
    const std::size_t dot = is_directory ? std::string_view::npos : name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0U) return {name, {}};
    return {name.substr(0, dot), name.substr(dot)};
}

// Finds an unused name in a directory: "stem.ext", then "stem 2.ext", "stem 3.ext" and so on, starting at
// first_number like iOS Files. Returns an empty string when no name is available.
std::string availableName(const std::string &directory, std::string_view stem, std::string_view extension,
                          int first_number)
{
    for (int number = first_number; number < 1000; ++number) {
        std::string candidate(stem);
        if (number > 1) {
            candidate += ' ';
            candidate += std::to_string(number);
        }
        candidate.append(extension);
        std::string path;
        if (candidate.size() >= FilesModel::name_capacity || !joinPath(path, directory, candidate)) return {};
        struct stat stats{};
        if (lstat(path.c_str(), &stats) == 0) continue;
        return errno == ENOENT ? candidate : std::string();
    }
    return {};
}

bool sameItem(const std::string &left, const std::string &right)
{
    struct stat left_stats{};
    struct stat right_stats{};
    return lstat(left.c_str(), &left_stats) == 0 && lstat(right.c_str(), &right_stats) == 0 &&
           left_stats.st_dev == right_stats.st_dev && left_stats.st_ino == right_stats.st_ino;
}

// Every entry of a directory, hidden ones included, except "." and "..".
std::vector<std::string> childNames(const std::string &path, int &error_number)
{
    std::vector<std::string> names;
    DIR *directory = opendir(path.c_str());
    if (directory == nullptr) {
        error_number = errno;
        return names;
    }
    for (;;) {
        errno              = 0;
        const dirent *item = readdir(directory);
        if (item == nullptr) {
            error_number = errno;
            break;
        }
        if (std::strcmp(item->d_name, ".") != 0 && std::strcmp(item->d_name, "..") != 0) {
            names.emplace_back(item->d_name);
        }
    }
    closedir(directory);
    return names;
}

int copyFileContents(const std::string &source, const std::string &destination, const struct stat &stats, bool &created)
{
    const int input = open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) return errno;
    const int output = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, stats.st_mode & 0777);
    if (output < 0) {
        const int error_number = errno;
        close(input);
        return error_number;
    }
    created = true;

    std::vector<char> buffer(65536);
    int error_number = 0;
    while (error_number == 0) {
        const ssize_t length = read(input, buffer.data(), buffer.size());
        if (length == 0) break;
        if (length < 0) {
            if (errno != EINTR) error_number = errno;
            continue;
        }
        std::size_t written = 0;
        while (written < static_cast<std::size_t>(length)) {
            const ssize_t result = write(output, buffer.data() + written, static_cast<std::size_t>(length) - written);
            if (result < 0) {
                if (errno == EINTR) continue;
                error_number = errno;
                break;
            }
            written += static_cast<std::size_t>(result);
        }
    }
    // Copies keep the modification date of their original.
    if (error_number == 0) {
        const timespec times[2] = {{static_cast<time_t>(stats.st_atime), 0}, {static_cast<time_t>(stats.st_mtime), 0}};
        (void)futimens(output, times);
    }
    close(input);
    if (close(output) != 0 && error_number == 0) error_number = errno;
    return error_number;
}

// Copies a file, a folder with its contents or a symbolic link (not its target). Everything is created anew,
// so nothing that already exists is written to; created tells whether destination itself was created.
int copyTree(const std::string &source, const std::string &destination, int depth, bool &created)
{
    created = false;
    struct stat stats{};
    if (lstat(source.c_str(), &stats) != 0) return errno;
    if (S_ISLNK(stats.st_mode)) {
        std::vector<char> target(PATH_MAX + 1);
        const ssize_t length = readlink(source.c_str(), target.data(), target.size() - 1U);
        if (length < 0) return errno;
        target[static_cast<std::size_t>(length)] = '\0';
        if (symlink(target.data(), destination.c_str()) != 0) return errno;
        created = true;
        return 0;
    }
    if (S_ISREG(stats.st_mode)) return copyFileContents(source, destination, stats, created);
    if (!S_ISDIR(stats.st_mode)) return ENOTSUP;  // Devices, sockets and pipes cannot be copied.
    if (depth >= kMaximumTreeDepth) return ELOOP;

    if (mkdir(destination.c_str(), (stats.st_mode & 0777) | S_IRWXU) != 0) return errno;
    created                              = true;
    int error_number                     = 0;
    const std::vector<std::string> names = childNames(source, error_number);
    for (std::size_t index = 0; error_number == 0 && index < names.size(); ++index) {
        bool child_created = false;
        error_number =
            copyTree(source + '/' + names[index], destination + '/' + names[index], depth + 1, child_created);
    }
    (void)chmod(destination.c_str(), stats.st_mode & 07777);
    return error_number;
}

// Deletes a file, a symbolic link (not its target) or a folder with everything in it.
int removeTree(const std::string &path, int depth)
{
    struct stat stats{};
    if (lstat(path.c_str(), &stats) != 0) return errno;
    if (!S_ISDIR(stats.st_mode)) return unlink(path.c_str()) == 0 ? 0 : errno;
    if (depth >= kMaximumTreeDepth) return ELOOP;

    int error_number                     = 0;
    const std::vector<std::string> names = childNames(path, error_number);
    for (std::size_t index = 0; error_number == 0 && index < names.size(); ++index) {
        error_number = removeTree(path + '/' + names[index], depth + 1);
    }
    if (error_number != 0) return error_number;
    return rmdir(path.c_str()) == 0 ? 0 : errno;
}

// Copies like copyTree() and removes a partial copy when it fails.
int copyItem(const std::string &source, const std::string &destination)
{
    bool created           = false;
    const int error_number = copyTree(source, destination, 0, created);
    if (error_number != 0 && created) (void)removeTree(destination, 0);
    return error_number;
}

// Adds up the visible contents of a folder for Get Info, stopping after maximum_info_items items.
void accumulateFolder(const std::string &path, FilesItemInfo &info, int depth)
{
    int error_number = 0;
    for (const std::string &name : childNames(path, error_number)) {
        if (name.front() == '.') continue;
        if (*info.item_count >= FilesModel::maximum_info_items) {
            info.complete = false;
            return;
        }
        const std::string child = path + '/' + name;
        struct stat stats{};
        if (lstat(child.c_str(), &stats) != 0) continue;
        ++*info.item_count;
        info.disk_size += static_cast<std::uint64_t>(stats.st_blocks) * 512U;
        if (!S_ISDIR(stats.st_mode)) {
            info.size += stats.st_size > 0 ? static_cast<std::uint64_t>(stats.st_size) : 0U;
        } else if (depth < kMaximumTreeDepth) {
            accumulateFolder(child, info, depth + 1);
        }
        if (!info.complete) return;
    }
}

// The birth time where the platform and file system record one.
std::optional<std::int64_t> creationTime(const std::string &path, const struct stat &stats)
{
#if defined(__APPLE__)
    (void)path;
    return static_cast<std::int64_t>(stats.st_birthtimespec.tv_sec);
#elif defined(__linux__) && defined(STATX_BTIME)
    (void)stats;
    struct statx extended{};
    if (statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BTIME, &extended) == 0 &&
        (extended.stx_mask & STATX_BTIME) != 0U) {
        return static_cast<std::int64_t>(extended.stx_btime.tv_sec);
    }
    return std::nullopt;
#else
    (void)path;
    (void)stats;
    return std::nullopt;
#endif
}

}  // namespace

FilesCommandResult::FilesCommandResult(FilesCommandStatus status, std::string message)
    : status_(status), message_(std::move(message))
{
}

FilesCommandResult FilesCommandResult::success()
{
    return FilesCommandResult(FilesCommandStatus::Success, {});
}

FilesCommandResult FilesCommandResult::failure(FilesCommandStatus status, std::string message)
{
    return FilesCommandResult(status, std::move(message));
}

bool FilesCommandResult::succeeded() const noexcept
{
    return status_ == FilesCommandStatus::Success;
}

FilesCommandStatus FilesCommandResult::status() const noexcept
{
    return status_;
}

const std::string &FilesCommandResult::message() const noexcept
{
    return message_;
}

FilesModel::FilesModel() = default;

FilesModel::FilesModel(std::string_view root_path)
{
    initialize(root_path);
}

FilesCommandResult FilesModel::initialize(std::string_view root_path)
{
    initialized_    = false;
    screen_         = FilesScreen::Home;
    content_screen_ = FilesScreen::Home;
    root_path_.clear();
    current_path_.clear();
    display_path_ = "~/";
    query_.clear();
    folder_name_ = default_folder_name;
    rename_name_.clear();
    action_names_.clear();
    selection_action_ = false;
    clipboard_paths_.clear();
    move_path_.clear();
    move_display_path_.clear();
    move_entries_.clear();
    info_     = {};
    has_info_ = false;
    preview_.clear();
    entries_.clear();
    selected_.clear();
    preview_index_  = maximum_entries;
    sort_key_       = FilesSortKey::Name;
    sort_ascending_ = true;
    view_mode_      = FilesViewMode::Icons;
    clearError();

    if (root_path.empty() || root_path.find('\0') != std::string_view::npos) {
        setError("Home directory is invalid");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    if (root_path.size() >= path_capacity) {
        setError("Home directory path is too long");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    const std::string requested_path(root_path);
    char resolved[path_capacity]{};
    if (realpath(requested_path.c_str(), resolved) == nullptr) {
        const FilesCommandResult result = systemFailure("Unable to open home directory", errno);
        setError(result.message());
        return result;
    }
    if (std::strlen(resolved) >= path_capacity) {
        setError("Home directory path is too long");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    root_path_                = resolved;
    current_path_             = resolved;
    FilesCommandResult result = refresh();
    if (!result.succeeded()) return result;
    initialized_ = true;
    return FilesCommandResult::success();
}

bool FilesModel::initialized() const noexcept
{
    return initialized_;
}

FilesScreen FilesModel::screen() const noexcept
{
    return screen_;
}

FilesScreen FilesModel::contentScreen() const noexcept
{
    return content_screen_;
}

const std::string &FilesModel::query() const noexcept
{
    return query_;
}

const std::string &FilesModel::folderName() const noexcept
{
    return folder_name_;
}

const std::string &FilesModel::preview() const noexcept
{
    return preview_;
}

const std::string &FilesModel::error() const noexcept
{
    return error_;
}

const std::string &FilesModel::displayPath() const noexcept
{
    return display_path_;
}

std::string FilesModel::directoryName() const
{
    if (!initialized_ || current_path_ == root_path_) return "Files";
    const std::size_t slash = current_path_.find_last_of('/');
    return slash == std::string::npos ? current_path_ : current_path_.substr(slash + 1U);
}

FilesSortKey FilesModel::sortKey() const noexcept
{
    return sort_key_;
}

bool FilesModel::sortAscending() const noexcept
{
    return sort_ascending_;
}

FilesViewMode FilesModel::viewMode() const noexcept
{
    return view_mode_;
}

std::size_t FilesModel::entryCount() const noexcept
{
    return entries_.size();
}

const FilesEntry *FilesModel::entry(std::size_t index) const noexcept
{
    return index < entries_.size() ? &entries_[index] : nullptr;
}

std::size_t FilesModel::searchCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(),
                      [this](const FilesEntry &candidate) { return containsCaseInsensitive(candidate.name, query_); }));
}

const FilesEntry *FilesModel::searchEntry(std::size_t index) const noexcept
{
    for (const FilesEntry &candidate : entries_) {
        if (!containsCaseInsensitive(candidate.name, query_)) continue;
        if (index == 0U) return &candidate;
        --index;
    }
    return nullptr;
}

const FilesEntry *FilesModel::previewEntry() const noexcept
{
    return preview_index_ < entries_.size() ? &entries_[preview_index_] : nullptr;
}

const FilesEntry *FilesModel::actionEntry() const noexcept
{
    return entry(actionIndex());
}

std::size_t FilesModel::actionIndex() const noexcept
{
    return action_names_.size() == 1U ? findEntry(action_names_.front()) : maximum_entries;
}

std::size_t FilesModel::actionCount() const noexcept
{
    return action_names_.size();
}

bool FilesModel::selecting() const noexcept
{
    return screen_ == FilesScreen::Select || selection_action_;
}

const std::string &FilesModel::renameName() const noexcept
{
    return rename_name_;
}

const FilesItemInfo *FilesModel::info() const noexcept
{
    return has_info_ ? &info_ : nullptr;
}

bool FilesModel::hasClipboard() const noexcept
{
    return !clipboard_paths_.empty();
}

std::size_t FilesModel::clipboardCount() const noexcept
{
    return clipboard_paths_.size();
}

const std::string &FilesModel::moveDisplayPath() const noexcept
{
    return move_display_path_;
}

std::string FilesModel::moveDirectoryName() const
{
    if (move_path_.empty() || move_path_ == root_path_) return "Files";
    return move_path_.substr(move_path_.find_last_of('/') + 1U);
}

std::string FilesModel::moveParentName() const
{
    const std::size_t slash = move_path_.find_last_of('/');
    if (moveAtRoot() || slash == std::string::npos) return "Files";
    const std::string parent = slash == 0U ? "/" : move_path_.substr(0, slash);
    if (parent == root_path_ || !pathIsWithinRoot(parent, root_path_)) return "Files";
    return parent.substr(parent.find_last_of('/') + 1U);
}

bool FilesModel::moveAtRoot() const noexcept
{
    return move_path_.empty() || move_path_ == root_path_;
}

std::size_t FilesModel::moveDestinationCount() const noexcept
{
    return move_entries_.size();
}

const FilesEntry *FilesModel::moveDestination(std::size_t index) const noexcept
{
    return index < move_entries_.size() ? &move_entries_[index] : nullptr;
}

bool FilesModel::moveDestinationEnabled(std::size_t index) const noexcept
{
    if (action_names_.empty() || index >= move_entries_.size()) return false;
    // A folder cannot go inside itself, so a folder being moved cannot be opened as a destination. Only
    // folders are listed, so a destination named like an action item in the open folder is that item.
    return move_path_ != current_path_ ||
           std::find(action_names_.begin(), action_names_.end(), move_entries_[index].name) == action_names_.end();
}

bool FilesModel::canMoveHere() const
{
    if (action_names_.empty() || move_path_.empty() || move_path_ == current_path_) return false;
    for (const std::string &name : action_names_) {
        const FilesEntry *target = entry(findEntry(name));
        std::string source;
        if (target == nullptr || !joinPath(source, current_path_, name)) return false;
        if (target->is_directory && pathIsWithinRoot(move_path_, source)) return false;
    }
    return true;
}

bool FilesModel::isSelected(std::size_t index) const noexcept
{
    return index < selected_.size() && selected_[index];
}

std::size_t FilesModel::selectedCount() const noexcept
{
    return static_cast<std::size_t>(std::count(selected_.begin(), selected_.end(), true));
}

FilesCommandResult FilesModel::show(FilesScreen screen)
{
    if (!isKnownScreen(screen)) {
        setError("Invalid screen");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    if (!initialized_) {
        setError("Files are not initialized");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    if (screen == FilesScreen::Folder && content_screen_ != FilesScreen::Folder) {
        setError("No folder is currently open");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    if (screen == FilesScreen::Preview && previewEntry() == nullptr) {
        setError("No file is available to preview");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    // The selection toolbar's Move, Delete and menu act on the selected items.
    const bool takes_selection = screen_ == FilesScreen::Select && actsOnSelection(screen);
    if (takes_selection) {
        if (selectedCount() == 0U) return fail(FilesCommandStatus::InvalidArgument, "No items are selected");
        action_names_     = selectedNames();
        selection_action_ = true;
    }
    if (actsOnItem(screen)) {
        FilesCommandResult result = prepareItemScreen(screen);
        if (!result.succeeded()) {
            if (takes_selection) {
                action_names_.clear();
                selection_action_ = false;
            }
            return result;
        }
    }

    if (screen == FilesScreen::Home) {
        action_names_.clear();
        while (current_path_ != root_path_) {
            FilesCommandResult result = goToParent();
            if (!result.succeeded()) return result;
        }
        updateContentScreen();
    }
    if (screen == FilesScreen::Select) {
        std::fill(selected_.begin(), selected_.end(), false);
        action_names_.clear();
    }
    // Only item screens act on the selection; any other screen leaves selection mode behind.
    if (!actsOnItem(screen)) selection_action_ = false;
    screen_ = screen;
    clearError();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::back()
{
    if (!initialized_) {
        setError("Files are not initialized");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    switch (screen_) {
        case FilesScreen::Home:
            clearError();
            return FilesCommandResult::success();
        case FilesScreen::Folder: {
            FilesCommandResult result = goToParent();
            if (!result.succeeded()) return result;
            updateContentScreen();
            screen_ = content_screen_;
            return FilesCommandResult::success();
        }
        case FilesScreen::Select:
            std::fill(selected_.begin(), selected_.end(), false);
            screen_ = content_screen_;
            clearError();
            return FilesCommandResult::success();
        case FilesScreen::Search:
        case FilesScreen::Preview:
        case FilesScreen::More:
            screen_ = content_screen_;
            clearError();
            return FilesCommandResult::success();
        case FilesScreen::Actions:
        case FilesScreen::Info:
        case FilesScreen::Rename:
        case FilesScreen::Move:
        case FilesScreen::ConfirmDelete:
        case FilesScreen::SelectionActions:
            if (selection_action_) {
                returnToSelection();
            } else {
                returnToContent();
            }
            return FilesCommandResult::success();
        case FilesScreen::NewFolder:
            folder_name_ = default_folder_name;
            screen_      = content_screen_;
            clearError();
            return FilesCommandResult::success();
    }
    setError("Invalid screen");
    return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
}

void FilesModel::setQuery(std::string_view query)
{
    query_ = boundedText(query, text_capacity - 1U);
    clearError();
}

void FilesModel::setFolderName(std::string_view name)
{
    folder_name_ = boundedText(name, text_capacity - 1U);
    clearError();
}

void FilesModel::setRenameName(std::string_view name)
{
    rename_name_ = boundedText(name, name_capacity - 1U);
    clearError();
}

FilesCommandResult FilesModel::openEntry(std::size_t index)
{
    if (!initialized_ || index >= entries_.size()) {
        setError("File item not found");
        return FilesCommandResult::failure(FilesCommandStatus::NotFound, error_);
    }

    if (entries_[index].is_directory) {
        FilesCommandResult result = enterDirectory(index);
        if (!result.succeeded()) return result;
        updateContentScreen();
        screen_ = FilesScreen::Folder;
        preview_.clear();
        preview_index_ = maximum_entries;
        clearError();
        return FilesCommandResult::success();
    }

    FilesCommandResult result = readPreview(index);
    if (!result.succeeded()) return result;
    preview_index_ = index;
    screen_        = FilesScreen::Preview;
    clearError();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::openSearchEntry(std::size_t index)
{
    for (std::size_t entry_index = 0; entry_index < entries_.size(); ++entry_index) {
        if (!containsCaseInsensitive(entries_[entry_index].name, query_)) {
            continue;
        }
        if (index == 0U) return openEntry(entry_index);
        --index;
    }
    setError("Search result not found");
    return FilesCommandResult::failure(FilesCommandStatus::NotFound, error_);
}

FilesCommandResult FilesModel::activateEntry(std::size_t index)
{
    // In selection mode a tap toggles the item instead of opening it.
    if (screen_ != FilesScreen::Select) return openEntry(index);
    if (!toggleSelection(index)) {
        setError("File item not found");
        return FilesCommandResult::failure(FilesCommandStatus::NotFound, error_);
    }
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::createFolder()
{
    if (!initialized_) {
        setError("Files are not initialized");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    if (folder_name_.empty() || folder_name_ == "." || folder_name_ == ".." ||
        folder_name_.find('/') != std::string::npos || folder_name_.find('\0') != std::string::npos) {
        setError("Folder name is invalid");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    std::string path;
    if (!joinPath(path, current_path_, folder_name_)) {
        setError("Path is too long");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    if (mkdir(path.c_str(), 0755) != 0) {
        FilesCommandResult result = systemFailure("Unable to create folder", errno);
        setError(result.message());
        return result;
    }

    FilesCommandResult result = refresh();
    if (!result.succeeded()) return result;
    updateContentScreen();
    folder_name_ = default_folder_name;
    screen_      = content_screen_;
    clearError();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::setSortKey(FilesSortKey key)
{
    if (!isKnownSortKey(key)) {
        setError("Invalid sort order");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    if (!initialized_) {
        setError("Files are not initialized");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    // Choosing the active key again reverses its direction, like the iOS sort menu.
    if (key == sort_key_) {
        sort_ascending_ = !sort_ascending_;
    } else {
        sort_key_       = key;
        sort_ascending_ = defaultAscending(key);
    }

    const FilesEntry *previewed      = previewEntry();
    const std::string previewed_name = previewed != nullptr ? previewed->name : std::string();
    sortEntries();
    resetSelection();
    preview_index_ = maximum_entries;
    for (std::size_t index = 0; !previewed_name.empty() && index < entries_.size(); ++index) {
        if (entries_[index].name == previewed_name) preview_index_ = index;
    }
    if (screen_ == FilesScreen::More) screen_ = content_screen_;
    clearError();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::setViewMode(FilesViewMode mode)
{
    if (!isKnownViewMode(mode)) {
        setError("Invalid view mode");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    if (!initialized_) {
        setError("Files are not initialized");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }
    view_mode_ = mode;
    if (screen_ == FilesScreen::More) screen_ = content_screen_;
    clearError();
    return FilesCommandResult::success();
}

bool FilesModel::toggleSelection(std::size_t index) noexcept
{
    if (index >= selected_.size()) return false;
    selected_[index] = !selected_[index];
    clearError();
    return true;
}

void FilesModel::selectAll() noexcept
{
    std::fill(selected_.begin(), selected_.end(), true);
    clearError();
}

void FilesModel::clearSelection() noexcept
{
    std::fill(selected_.begin(), selected_.end(), false);
    clearError();
}

void FilesModel::toggleSelectAll() noexcept
{
    if (!selected_.empty() && selectedCount() == selected_.size()) {
        clearSelection();
    } else {
        selectAll();
    }
}

FilesActionCapability FilesModel::actionCapability(FilesAction action) const noexcept
{
    const auto value = static_cast<std::int32_t>(action);
    if (value < 0 || value >= static_cast<std::int32_t>(FilesAction::Count)) return {false, false, "Unknown action"};
    if (!initialized_) return {true, false, "Files are not initialized"};
    switch (action) {
        case FilesAction::ChangeSort:
        case FilesAction::ChangeView:
        case FilesAction::Count:
            break;
        case FilesAction::Paste:
            if (clipboard_paths_.empty()) return {true, false, "Nothing has been copied"};
            break;
        case FilesAction::Copy:
        case FilesAction::Move:
        case FilesAction::Compress:
        case FilesAction::Remove:
        case FilesAction::Duplicate:
        case FilesAction::NewFolderWithItem:
            // These act on every action item, or on the selected items in selection mode.
            if ((screen_ == FilesScreen::Select ? selectedCount() : action_names_.size()) == 0U) {
                return {true, false, "No item is chosen"};
            }
            break;
        case FilesAction::Rename:
        case FilesAction::GetInfo:
            if (actionEntry() == nullptr) return {true, false, "No item is chosen"};
            break;
    }
    return {true, true, {}};
}

std::uint32_t FilesModel::supportedActionMask() const noexcept
{
    std::uint32_t mask = 0;
    for (std::int32_t value = 0; value < static_cast<std::int32_t>(FilesAction::Count); ++value) {
        const FilesAction action = static_cast<FilesAction>(value);
        if (actionCapability(action).supported) mask |= actionBit(action);
    }
    return mask;
}

std::uint32_t FilesModel::enabledActionMask() const noexcept
{
    std::uint32_t mask = 0;
    for (std::int32_t value = 0; value < static_cast<std::int32_t>(FilesAction::Count); ++value) {
        const FilesAction action = static_cast<FilesAction>(value);
        if (actionCapability(action).enabled) mask |= actionBit(action);
    }
    return mask;
}

FilesCommandResult FilesModel::showActions(std::size_t index)
{
    if (!initialized_ || index >= entries_.size()) return fail(FilesCommandStatus::NotFound, "File item not found");
    if (screen_ != FilesScreen::Home && screen_ != FilesScreen::Folder) {
        return fail(FilesCommandStatus::InvalidArgument, "Item actions are not available here");
    }
    action_names_.assign(1U, entries_[index].name);
    selection_action_ = false;
    return show(FilesScreen::Actions);
}

FilesCommandResult FilesModel::openActionEntry()
{
    const std::size_t index = actionIndex();
    if (!initialized_ || index >= entries_.size()) return fail(FilesCommandStatus::NotFound, "File item not found");
    FilesCommandResult result = openEntry(index);
    if (!result.succeeded()) return result;
    action_names_.clear();
    selection_action_ = false;
    has_info_         = false;
    return result;
}

FilesCommandResult FilesModel::copyActionItems()
{
    std::vector<ActionItem> items;
    FilesCommandResult result = requireActionItems(items);
    if (!result.succeeded()) return result;
    clipboard_paths_.clear();
    for (ActionItem &item : items) clipboard_paths_.push_back(std::move(item.path));
    returnToContent();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::paste()
{
    if (!initialized_) return fail(FilesCommandStatus::InvalidArgument, "Files are not initialized");
    if (clipboard_paths_.empty()) return fail(FilesCommandStatus::InvalidArgument, "Nothing has been copied");

    // Every copied item must still exist and no folder may go into itself, or nothing is pasted.
    std::vector<bool> directories;
    for (const std::string &source : clipboard_paths_) {
        struct stat stats{};
        if (lstat(source.c_str(), &stats) != 0) {
            const int error_number = errno;
            clipboard_paths_.clear();
            return systemError("Unable to paste", error_number);
        }
        directories.push_back(S_ISDIR(stats.st_mode));
        if (directories.back() && pathIsWithinRoot(current_path_, source)) {
            return fail(FilesCommandStatus::InvalidArgument, "A folder cannot be pasted into itself");
        }
    }

    for (std::size_t index = 0; index < clipboard_paths_.size(); ++index) {
        // Pasting next to an item with the same name adds a number, as in iOS Files.
        const std::string &source    = clipboard_paths_[index];
        const auto [stem, extension] = splitExtension(nameOf(source), directories[index]);
        const std::string name       = availableName(current_path_, stem, extension, 1);
        std::string destination;
        if (name.empty() || !joinPath(destination, current_path_, name)) {
            return failAfter(index, FilesCommandResult::failure(FilesCommandStatus::InvalidArgument,
                                                                "Unable to choose a name for the copy"));
        }
        const int error_number = copyItem(source, destination);
        if (error_number != 0) return failAfter(index, systemFailure("Unable to paste", error_number));
    }
    return finishOperation();
}

FilesCommandResult FilesModel::renameActionEntry()
{
    const FilesEntry *target = nullptr;
    std::string source;
    FilesCommandResult result = requireActionEntry(target, source);
    if (!result.succeeded()) return result;
    if (!isValidName(rename_name_)) return fail(FilesCommandStatus::InvalidArgument, "Name is invalid");
    if (rename_name_ == target->name) {
        returnToContent();
        return FilesCommandResult::success();
    }

    std::string destination;
    if (!joinPath(destination, current_path_, rename_name_)) {
        return fail(FilesCommandStatus::InvalidArgument, "Path is too long");
    }
    struct stat existing{};
    if (lstat(destination.c_str(), &existing) == 0) {
        // A change of letter case on a case-insensitive file system finds the item itself.
        if (!sameItem(source, destination)) {
            return fail(FilesCommandStatus::InvalidArgument,
                        "The name \u201c" + rename_name_ + "\u201d is already taken");
        }
    } else if (errno != ENOENT) {
        return systemError("Unable to rename", errno);
    }
    if (std::rename(source.c_str(), destination.c_str()) != 0) return systemError("Unable to rename", errno);
    retargetClipboard(source, destination);
    return finishOperation();
}

FilesCommandResult FilesModel::compressActionItems()
{
    std::vector<ActionItem> items;
    FilesCommandResult result = requireActionItems(items);
    if (!result.succeeded()) return result;

    // One item is archived under its own name; several go into "Archive.zip", as in iOS Files.
    const std::string name = availableName(
        current_path_, items.size() == 1U ? std::string_view(items.front().name) : default_archive_name, ".zip", 1);
    std::string archive;
    if (name.empty() || !joinPath(archive, current_path_, name)) {
        return fail(FilesCommandStatus::InvalidArgument, "Unable to choose a name for the archive");
    }
    std::vector<files_archive::ZipSource> sources;
    for (ActionItem &item : items) sources.push_back({std::move(item.path), std::move(item.name)});
    const std::string failure = files_archive::create_zip(archive, sources);
    if (!failure.empty()) return fail(FilesCommandStatus::IoError, "Unable to compress: " + failure);
    return finishOperation();
}

FilesCommandResult FilesModel::duplicateActionItems()
{
    std::vector<ActionItem> items;
    FilesCommandResult result = requireActionItems(items);
    if (!result.succeeded()) return result;

    for (std::size_t index = 0; index < items.size(); ++index) {
        const ActionItem &item       = items[index];
        const auto [stem, extension] = splitExtension(item.name, item.is_directory);
        const std::string name       = availableName(current_path_, stem, extension, 2);
        std::string destination;
        if (name.empty() || !joinPath(destination, current_path_, name)) {
            return failAfter(index, FilesCommandResult::failure(FilesCommandStatus::InvalidArgument,
                                                                "Unable to choose a name for the copy"));
        }
        const int error_number = copyItem(item.path, destination);
        if (error_number != 0) return failAfter(index, systemFailure("Unable to duplicate", error_number));
    }
    return finishOperation();
}

FilesCommandResult FilesModel::newFolderWithActionItems()
{
    std::vector<ActionItem> items;
    FilesCommandResult result = requireActionItems(items);
    if (!result.succeeded()) return result;

    const std::string folder = availableName(current_path_, default_folder_name, {}, 1);
    std::string folder_path;
    if (folder.empty() || !joinPath(folder_path, current_path_, folder)) {
        return fail(FilesCommandStatus::InvalidArgument, "Unable to choose a name for the folder");
    }
    std::vector<std::string> destinations(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!joinPath(destinations[index], folder_path, items[index].name)) {
            return fail(FilesCommandStatus::InvalidArgument, "Unable to choose a name for the folder");
        }
    }
    if (mkdir(folder_path.c_str(), 0755) != 0) return systemError("Unable to create folder", errno);
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (std::rename(items[index].path.c_str(), destinations[index].c_str()) == 0) continue;
        // Put back the items already moved, so a failure leaves the folder as it was.
        const int error_number = errno;
        for (std::size_t moved = 0; moved < index; ++moved) {
            (void)std::rename(destinations[moved].c_str(), items[moved].path.c_str());
        }
        (void)rmdir(folder_path.c_str());
        return failAfter(index, systemFailure("Unable to move the item", error_number));
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        retargetClipboard(items[index].path, destinations[index]);
    }

    result = refresh();
    if (!result.succeeded()) return result;
    updateContentScreen();
    // Like iOS Files, the new folder's name is ready to be changed.
    action_names_.assign(1U, folder);
    selection_action_ = false;
    rename_name_      = folder;
    has_info_         = false;
    screen_           = FilesScreen::Rename;
    clearError();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::removeActionItems()
{
    std::vector<ActionItem> items;
    FilesCommandResult result = requireActionItems(items);
    if (!result.succeeded()) return result;

    for (std::size_t index = 0; index < items.size(); ++index) {
        const int error_number = removeTree(items[index].path, 0);
        // Part of a folder may already be gone; show what is left.
        if (error_number != 0) return failAfter(index + 1U, systemFailure("Unable to delete", error_number));
    }
    return finishOperation();
}

FilesCommandResult FilesModel::openMoveDestination(std::size_t index)
{
    if (screen_ != FilesScreen::Move || index >= move_entries_.size()) {
        return fail(FilesCommandStatus::NotFound, "Folder not found");
    }
    if (!moveDestinationEnabled(index)) {
        return fail(FilesCommandStatus::InvalidArgument, "A folder cannot be moved into itself");
    }
    std::string candidate;
    if (!joinPath(candidate, move_path_, move_entries_[index].name)) {
        return fail(FilesCommandStatus::InvalidArgument, "Path is too long");
    }
    char resolved[path_capacity]{};
    if (realpath(candidate.c_str(), resolved) == nullptr) return systemError("Unable to open folder", errno);
    if (!pathIsWithinRoot(resolved, root_path_)) {
        return fail(FilesCommandStatus::InvalidArgument, "Folders outside the home directory cannot be opened");
    }
    FilesCommandResult result = loadMoveDestinations(resolved);
    if (result.succeeded()) clearError();
    return result;
}

FilesCommandResult FilesModel::moveDestinationUp()
{
    if (screen_ != FilesScreen::Move) return fail(FilesCommandStatus::InvalidArgument, "No folder is being chosen");
    if (move_path_ == root_path_) {
        clearError();
        return FilesCommandResult::success();
    }
    const std::size_t slash = move_path_.find_last_of('/');
    std::string parent      = slash == 0U ? "/" : move_path_.substr(0, slash);
    if (slash == std::string::npos || !pathIsWithinRoot(parent, root_path_)) parent = root_path_;
    FilesCommandResult result = loadMoveDestinations(parent);
    if (result.succeeded()) clearError();
    return result;
}

FilesCommandResult FilesModel::moveActionItems()
{
    std::vector<ActionItem> items;
    FilesCommandResult result = requireActionItems(items);
    if (!result.succeeded()) return result;
    if (screen_ != FilesScreen::Move) return fail(FilesCommandStatus::InvalidArgument, "No folder is being chosen");
    if (move_path_ == current_path_) {
        return fail(FilesCommandStatus::InvalidArgument,
                    items.size() == 1U ? "The item is already in this folder" : "The items are already in this folder");
    }

    // Every item is checked before any moves, so a refused move changes nothing.
    std::vector<std::string> destinations(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const ActionItem &item = items[index];
        if (item.is_directory && pathIsWithinRoot(move_path_, item.path)) {
            return fail(FilesCommandStatus::InvalidArgument, "A folder cannot be moved into itself");
        }
        if (!joinPath(destinations[index], move_path_, item.name)) {
            return fail(FilesCommandStatus::InvalidArgument, "Path is too long");
        }
        struct stat existing{};
        if (lstat(destinations[index].c_str(), &existing) == 0) {
            return fail(FilesCommandStatus::InvalidArgument,
                        "An item named \u201c" + item.name + "\u201d already exists there");
        }
        if (errno != ENOENT) return systemError("Unable to move", errno);
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        const std::string &source      = items[index].path;
        const std::string &destination = destinations[index];
        if (std::rename(source.c_str(), destination.c_str()) != 0) {
            int error_number = errno;
            if (error_number != EXDEV) return failAfter(index, systemFailure("Unable to move", error_number));
            // The destination is on another file system: copy the item, then remove the original.
            error_number = copyItem(source, destination);
            if (error_number != 0) return failAfter(index, systemFailure("Unable to move", error_number));
            error_number = removeTree(source, 0);
            if (error_number != 0) {
                return failAfter(index + 1U, systemFailure("The item was copied, but the original could not be removed",
                                                           error_number));
            }
        }
        retargetClipboard(source, destination);
    }
    return finishOperation();
}

FilesCommandResult FilesModel::refresh()
{
    DIR *directory = opendir(current_path_.c_str());
    if (directory == nullptr) {
        FilesCommandResult result = systemFailure("Unable to read directory", errno);
        setError(result.message());
        return result;
    }

    std::vector<FilesEntry> refreshed;
    refreshed.reserve(maximum_entries);
    int read_error = 0;
    while (refreshed.size() < maximum_entries) {
        errno        = 0;
        dirent *item = readdir(directory);
        if (item == nullptr) {
            read_error = errno;
            break;
        }
        if (item->d_name[0] == '.') continue;

        std::string path;
        if (!joinPath(path, current_path_, item->d_name)) continue;
        struct stat stats{};
        if (lstat(path.c_str(), &stats) != 0) continue;

        FilesEntry entry;
        entry.name          = item->d_name;
        entry.is_directory  = S_ISDIR(stats.st_mode);
        entry.is_symlink    = S_ISLNK(stats.st_mode);
        entry.size          = entry.is_directory || stats.st_size < 0 ? 0U : static_cast<std::uint64_t>(stats.st_size);
        entry.modified_time = static_cast<std::int64_t>(stats.st_mtime);
        entry.kind          = classifyEntry(entry.name, entry.is_directory, entry.is_symlink);
        if (entry.is_directory) entry.item_count = countVisibleEntries(path);
        refreshed.push_back(std::move(entry));
    }
    if (closedir(directory) != 0 && read_error == 0) read_error = errno;
    if (read_error != 0) {
        FilesCommandResult result = systemFailure("Unable to read directory", read_error);
        setError(result.message());
        return result;
    }

    entries_ = std::move(refreshed);
    sortEntries();
    resetSelection();
    preview_.clear();
    preview_index_ = maximum_entries;

    display_path_ = displayPathOf(current_path_);
    clearError();
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::enterDirectory(std::size_t index)
{
    if (index >= entries_.size() || !entries_[index].is_directory || entries_[index].is_symlink) {
        setError("Selected item is not a folder");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    std::string candidate;
    if (!joinPath(candidate, current_path_, entries_[index].name)) {
        setError("Path is too long");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    char resolved[path_capacity]{};
    if (realpath(candidate.c_str(), resolved) == nullptr) {
        FilesCommandResult result = systemFailure("Unable to open folder", errno);
        setError(result.message());
        return result;
    }
    if (!pathIsWithinRoot(resolved, root_path_)) {
        setError("Folders outside the home directory cannot be opened");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    const std::string previous = current_path_;
    current_path_              = resolved;
    FilesCommandResult result  = refresh();
    if (result.succeeded()) return result;

    const std::string original_error = result.message();
    current_path_                    = previous;
    refresh();
    setError(original_error);
    return FilesCommandResult::failure(FilesCommandStatus::IoError, original_error);
}

FilesCommandResult FilesModel::goToParent()
{
    if (current_path_ == root_path_) return FilesCommandResult::success();

    const std::size_t slash = current_path_.find_last_of('/');
    std::string parent      = slash == 0U ? "/" : current_path_.substr(0, slash);
    if (slash == std::string::npos || !pathIsWithinRoot(parent, root_path_)) {
        parent = root_path_;
    }

    const std::string previous = current_path_;
    current_path_              = std::move(parent);
    FilesCommandResult result  = refresh();
    if (result.succeeded()) return result;

    const std::string original_error = result.message();
    current_path_                    = previous;
    refresh();
    setError(original_error);
    return FilesCommandResult::failure(FilesCommandStatus::IoError, original_error);
}

FilesCommandResult FilesModel::readPreview(std::size_t index)
{
    const FilesEntry &selected_entry = entries_[index];
    if (selected_entry.is_symlink) {
        setError("Symbolic links cannot be previewed outside the home directory");
        return FilesCommandResult::failure(FilesCommandStatus::Unsupported, error_);
    }
    if (selected_entry.kind != FilesEntryKind::Text) {
        preview_ = "Preview is not available for this file type.";
        return FilesCommandResult::success();
    }

    std::string path;
    if (!joinPath(path, current_path_, selected_entry.name)) {
        setError("Path is too long");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    const int file_descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (file_descriptor < 0) {
        FilesCommandResult result = systemFailure("Unable to read file", errno);
        setError(result.message());
        return result;
    }

    struct stat stats{};
    if (fstat(file_descriptor, &stats) != 0) {
        const int error_number = errno;
        close(file_descriptor);
        FilesCommandResult result = systemFailure("Unable to read file", error_number);
        setError(result.message());
        return result;
    }
    if (!S_ISREG(stats.st_mode)) {
        close(file_descriptor);
        FilesCommandResult result = systemFailure("Unable to read file", EINVAL);
        setError(result.message());
        return result;
    }

    std::vector<char> buffer(preview_capacity);
    std::size_t used = 0;
    while (used < buffer.size()) {
        const ssize_t length = read(file_descriptor, buffer.data() + used, buffer.size() - used);
        if (length > 0) {
            used += static_cast<std::size_t>(length);
            continue;
        }
        if (length == 0) break;
        if (errno == EINTR) continue;

        const int error_number = errno;
        close(file_descriptor);
        FilesCommandResult result = systemFailure("Unable to read file", error_number);
        setError(result.message());
        return result;
    }
    close(file_descriptor);

    const bool truncated =
        used == buffer.size() || (stats.st_size >= 0 && static_cast<std::uint64_t>(stats.st_size) > used);
    const std::size_t text_size = std::min(used, static_cast<std::size_t>(preview_capacity - 1U));
    for (std::size_t position = 0; position < text_size; ++position) {
        if (buffer[position] == '\0') buffer[position] = ' ';
    }
    preview_.assign(buffer.data(), text_size);

    if (truncated) {
        static constexpr std::string_view marker = "\n\n[Preview truncated]";
        const std::size_t content_limit          = preview_capacity - 1U - marker.size();
        preview_                                 = boundedText(preview_, content_limit);
        preview_.append(marker);
    }
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::loadInfo(const FilesEntry &target)
{
    std::string path;
    if (!joinPath(path, current_path_, target.name))
        return fail(FilesCommandStatus::InvalidArgument, "Path is too long");
    struct stat stats{};
    if (lstat(path.c_str(), &stats) != 0) return systemError("Unable to read item information", errno);

    FilesItemInfo details;
    details.name          = target.name;
    details.kind          = target.kind;
    details.is_directory  = S_ISDIR(stats.st_mode);
    details.is_symlink    = S_ISLNK(stats.st_mode);
    details.location      = current_path_ == root_path_ ? "Files" : "Files/" + display_path_;
    details.modified_time = static_cast<std::int64_t>(stats.st_mtime);
    details.created_time  = creationTime(path, stats);
    details.disk_size     = static_cast<std::uint64_t>(stats.st_blocks) * 512U;
    if (details.is_directory) {
        details.item_count = 0U;
        accumulateFolder(path, details, 0);
    } else {
        details.size = stats.st_size > 0 ? static_cast<std::uint64_t>(stats.st_size) : 0U;
    }
    info_     = std::move(details);
    has_info_ = true;
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::loadMoveDestinations(const std::string &path)
{
    DIR *directory = opendir(path.c_str());
    if (directory == nullptr) return systemError("Unable to read directory", errno);

    std::vector<FilesEntry> folders;
    int read_error = 0;
    while (folders.size() < maximum_entries) {
        errno              = 0;
        const dirent *item = readdir(directory);
        if (item == nullptr) {
            read_error = errno;
            break;
        }
        if (item->d_name[0] == '.') continue;
        std::string child;
        struct stat stats{};
        if (!joinPath(child, path, item->d_name) || lstat(child.c_str(), &stats) != 0 || !S_ISDIR(stats.st_mode)) {
            continue;
        }
        FilesEntry folder;
        folder.name          = item->d_name;
        folder.kind          = FilesEntryKind::Folder;
        folder.is_directory  = true;
        folder.modified_time = static_cast<std::int64_t>(stats.st_mtime);
        folder.item_count    = countVisibleEntries(child);
        folders.push_back(std::move(folder));
    }
    closedir(directory);
    if (read_error != 0) return systemError("Unable to read directory", read_error);

    std::sort(folders.begin(), folders.end(),
              [](const FilesEntry &left, const FilesEntry &right) { return compareNames(left, right) < 0; });
    move_path_         = path;
    move_display_path_ = displayPathOf(path);
    move_entries_      = std::move(folders);
    return FilesCommandResult::success();
}

// Checks that a screen's action items are still listed and loads what the screen shows of them.
FilesCommandResult FilesModel::prepareItemScreen(FilesScreen screen)
{
    if (screen == FilesScreen::Actions || screen == FilesScreen::Info || screen == FilesScreen::Rename) {
        const FilesEntry *target = actionEntry();
        if (target == nullptr) return fail(FilesCommandStatus::NotFound, "File item not found");
        if (screen == FilesScreen::Info) return loadInfo(*target);
        if (screen == FilesScreen::Rename) rename_name_ = target->name;
        return FilesCommandResult::success();
    }
    if (action_names_.empty()) return fail(FilesCommandStatus::NotFound, "File item not found");
    for (const std::string &name : action_names_) {
        if (findEntry(name) == maximum_entries) return fail(FilesCommandStatus::NotFound, "File item not found");
    }
    return screen == FilesScreen::Move ? loadMoveDestinations(current_path_) : FilesCommandResult::success();
}

FilesCommandResult FilesModel::requireActionEntry(const FilesEntry *&target, std::string &path)
{
    target = initialized_ ? actionEntry() : nullptr;
    if (target == nullptr) return fail(FilesCommandStatus::NotFound, "File item not found");
    if (!joinPath(path, current_path_, target->name))
        return fail(FilesCommandStatus::InvalidArgument, "Path is too long");
    return FilesCommandResult::success();
}

// The action items in the open folder; in selection mode, the selected items.
FilesCommandResult FilesModel::requireActionItems(std::vector<ActionItem> &items)
{
    items.clear();
    if (!initialized_) return fail(FilesCommandStatus::NotFound, "File item not found");
    const bool selection_mode            = screen_ == FilesScreen::Select;
    const std::vector<std::string> names = selection_mode ? selectedNames() : action_names_;
    if (names.empty()) {
        return selection_mode ? fail(FilesCommandStatus::InvalidArgument, "No items are selected")
                              : fail(FilesCommandStatus::NotFound, "File item not found");
    }
    for (const std::string &name : names) {
        const FilesEntry *target = entry(findEntry(name));
        if (target == nullptr) return fail(FilesCommandStatus::NotFound, "File item not found");
        ActionItem item{name, {}, target->is_directory};
        if (!joinPath(item.path, current_path_, name)) {
            return fail(FilesCommandStatus::InvalidArgument, "Path is too long");
        }
        items.push_back(std::move(item));
    }
    return FilesCommandResult::success();
}

FilesCommandResult FilesModel::finishOperation()
{
    FilesCommandResult result = refresh();
    if (!result.succeeded()) return result;
    updateContentScreen();
    returnToContent();
    return FilesCommandResult::success();
}

// Fails a command after it changed some items: the folder is read again to show what is left first.
FilesCommandResult FilesModel::failAfter(std::size_t changed, const FilesCommandResult &failure)
{
    if (changed > 0U) keepRemainingItems();
    return fail(failure.status(), failure.message());
}

FilesCommandResult FilesModel::fail(FilesCommandStatus status, std::string message)
{
    setError(std::move(message));
    return FilesCommandResult::failure(status, error_);
}

FilesCommandResult FilesModel::systemError(std::string_view operation, int error_number)
{
    std::string message(operation);
    message += ": ";
    message += std::strerror(error_number);
    return fail(FilesCommandStatus::IoError, std::move(message));
}

void FilesModel::returnToContent() noexcept
{
    screen_ = content_screen_;
    std::fill(selected_.begin(), selected_.end(), false);
    action_names_.clear();
    selection_action_ = false;
    rename_name_.clear();
    move_entries_.clear();
    has_info_ = false;
    clearError();
}

// Leaves a menu, sheet or prompt that the selection toolbar opened; the items stay selected.
void FilesModel::returnToSelection() noexcept
{
    screen_ = FilesScreen::Select;
    action_names_.clear();
    selection_action_ = false;
    rename_name_.clear();
    move_entries_.clear();
    has_info_ = false;
    clearError();
}

// Reads the folder again after a command changed part of it. The action items still there stay chosen,
// and stay selected when they came from selection mode.
void FilesModel::keepRemainingItems()
{
    const bool selection_mode      = screen_ == FilesScreen::Select;
    std::vector<std::string> names = selection_mode ? selectedNames() : action_names_;
    (void)refresh();
    names.erase(std::remove_if(names.begin(), names.end(),
                               [this](const std::string &name) { return findEntry(name) == maximum_entries; }),
                names.end());
    if (!selection_mode) action_names_ = names;
    if (selecting()) reselect(names);
}

// Copied items that were renamed or moved are pasted from their new place.
void FilesModel::retargetClipboard(const std::string &from, const std::string &to)
{
    std::replace(clipboard_paths_.begin(), clipboard_paths_.end(), from, to);
}

void FilesModel::sortEntries()
{
    // Folders always come first; the key orders entries within each group and names break ties.
    const FilesSortKey key = sort_key_;
    const bool ascending   = sort_ascending_;
    std::sort(entries_.begin(), entries_.end(), [key, ascending](const FilesEntry &left, const FilesEntry &right) {
        if (left.is_directory != right.is_directory) return left.is_directory;
        const int order = compareBy(key, left, right);
        if (order != 0) return ascending ? order < 0 : order > 0;
        return compareNames(left, right) < 0;
    });
}

void FilesModel::updateContentScreen() noexcept
{
    content_screen_ = current_path_ == root_path_ ? FilesScreen::Home : FilesScreen::Folder;
}

std::size_t FilesModel::findEntry(std::string_view name) const noexcept
{
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].name == name) return index;
    }
    return maximum_entries;
}

std::vector<std::string> FilesModel::selectedNames() const
{
    std::vector<std::string> names;
    for (std::size_t index = 0; index < entries_.size() && index < selected_.size(); ++index) {
        if (selected_[index]) names.push_back(entries_[index].name);
    }
    return names;
}

std::string FilesModel::displayPathOf(const std::string &path) const
{
    if (path == root_path_) return "~/";
    std::size_t offset = root_path_.size();
    if (offset < path.size() && path[offset] == '/') ++offset;
    return path.substr(offset);
}

void FilesModel::resetSelection()
{
    selected_.assign(entries_.size(), false);
}

void FilesModel::reselect(const std::vector<std::string> &names)
{
    resetSelection();
    for (const std::string &name : names) {
        const std::size_t index = findEntry(name);
        if (index < selected_.size()) selected_[index] = true;
    }
}

void FilesModel::clearError() noexcept
{
    error_.clear();
}

void FilesModel::setError(std::string message)
{
    error_ = boundedText(message, name_capacity - 1U);
}

std::string FilesModel::boundedText(std::string_view value, std::size_t maximum_bytes)
{
    if (value.size() <= maximum_bytes) return std::string(value);

    std::size_t end = maximum_bytes;
    while (end > 0U && (static_cast<unsigned char>(value[end]) & 0xc0U) == 0x80U) {
        --end;
    }
    return std::string(value.substr(0, end));
}

bool FilesModel::containsCaseInsensitive(std::string_view text, std::string_view query) noexcept
{
    if (query.empty()) return true;
    if (query.size() > text.size()) return false;
    for (std::size_t offset = 0; offset + query.size() <= text.size(); ++offset) {
        if (strncasecmp(text.data() + offset, query.data(), query.size()) == 0) {
            return true;
        }
    }
    return false;
}

std::uint32_t FilesModel::actionBit(FilesAction action) noexcept
{
    const auto value = static_cast<std::uint32_t>(action);
    return value < 32U ? 1U << value : 0U;
}
