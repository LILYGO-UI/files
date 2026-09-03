#include "pages/files/files_model.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>

namespace {

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
            return true;
    }
    return false;
}

std::uint64_t saturatingProduct(std::uint64_t left, std::uint64_t right) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
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
    if (is_directory) {
        if (equalsCaseInsensitive(name, "Downloads") || name == "下载") {
            return FilesEntryKind::Download;
        }
        if (equalsCaseInsensitive(name, "Music") || name == "音乐") {
            return FilesEntryKind::Music;
        }
        return FilesEntryKind::Folder;
    }
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

bool entryLess(const FilesEntry &left, const FilesEntry &right)
{
    if (left.is_directory != right.is_directory) return left.is_directory;
    const int comparison = strcasecmp(left.name.c_str(), right.name.c_str());
    if (comparison != 0) return comparison < 0;
    return left.name < right.name;
}

std::string_view actionUnavailableReason(FilesAction action)
{
    switch (action) {
        case FilesAction::Copy:
            return "Copy is not available yet";
        case FilesAction::Rename:
            return "Rename is not available yet";
        case FilesAction::Move:
            return "Move is not available yet";
        case FilesAction::Archive:
            return "Archive is not available yet";
        case FilesAction::Remove:
            return "Delete is not available yet";
        case FilesAction::ChangeSort:
            return "Sort options are not available yet";
        case FilesAction::ChangeView:
            return "View options are not available yet";
        case FilesAction::Count:
            return "Unknown action";
    }
    return "Unknown action";
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
    preview_.clear();
    entries_.clear();
    selected_.clear();
    preview_index_  = maximum_entries;
    capacity_bytes_ = 0;
    used_bytes_     = 0;
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

std::uint64_t FilesModel::capacityBytes() const noexcept
{
    return capacity_bytes_;
}

std::uint64_t FilesModel::usedBytes() const noexcept
{
    return used_bytes_;
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

std::size_t FilesModel::destinationCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(),
                                                  [](const FilesEntry &candidate) { return candidate.is_directory; }));
}

const FilesEntry *FilesModel::destinationEntry(std::size_t index) const noexcept
{
    for (const FilesEntry &candidate : entries_) {
        if (!candidate.is_directory) continue;
        if (index == 0U) return &candidate;
        --index;
    }
    return nullptr;
}

const FilesEntry *FilesModel::previewEntry() const noexcept
{
    return preview_index_ < entries_.size() ? &entries_[preview_index_] : nullptr;
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

    if (screen == FilesScreen::Select || screen == FilesScreen::Move) {
        setError("This file operation is not available yet");
        return FilesCommandResult::failure(FilesCommandStatus::Unsupported, error_);
    }

    if (screen == FilesScreen::Folder && content_screen_ != FilesScreen::Folder) {
        setError("No folder is currently open");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    if (screen == FilesScreen::Preview && previewEntry() == nullptr) {
        setError("No file is available to preview");
        return FilesCommandResult::failure(FilesCommandStatus::InvalidArgument, error_);
    }

    if (screen == FilesScreen::Home) {
        while (current_path_ != root_path_) {
            FilesCommandResult result = goToParent();
            if (!result.succeeded()) return result;
        }
        updateContentScreen();
    }
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
        case FilesScreen::Search:
        case FilesScreen::Select:
        case FilesScreen::Preview:
        case FilesScreen::Move:
        case FilesScreen::More:
            screen_ = content_screen_;
            clearError();
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

FilesActionCapability FilesModel::actionCapability(FilesAction action) const noexcept
{
    return {false, false, actionUnavailableReason(action)};
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

FilesCommandResult FilesModel::copySelection() const
{
    return unsupported(FilesAction::Copy);
}

FilesCommandResult FilesModel::renameSelection(std::string_view new_name) const
{
    (void)new_name;
    return unsupported(FilesAction::Rename);
}

FilesCommandResult FilesModel::moveSelectionTo(std::size_t destination_index) const
{
    (void)destination_index;
    return unsupported(FilesAction::Move);
}

FilesCommandResult FilesModel::archiveSelection() const
{
    return unsupported(FilesAction::Archive);
}

FilesCommandResult FilesModel::removeSelection() const
{
    return unsupported(FilesAction::Remove);
}

FilesCommandResult FilesModel::changeSort() const
{
    return unsupported(FilesAction::ChangeSort);
}

FilesCommandResult FilesModel::changeView() const
{
    return unsupported(FilesAction::ChangeView);
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
        entry.name         = item->d_name;
        entry.is_directory = S_ISDIR(stats.st_mode);
        entry.is_symlink   = S_ISLNK(stats.st_mode);
        entry.size         = entry.is_directory || stats.st_size < 0 ? 0U : static_cast<std::uint64_t>(stats.st_size);
        entry.kind         = classifyEntry(entry.name, entry.is_directory, entry.is_symlink);
        refreshed.push_back(std::move(entry));
    }
    if (closedir(directory) != 0 && read_error == 0) read_error = errno;
    if (read_error != 0) {
        FilesCommandResult result = systemFailure("Unable to read directory", read_error);
        setError(result.message());
        return result;
    }

    std::sort(refreshed.begin(), refreshed.end(), entryLess);
    entries_ = std::move(refreshed);
    resetSelection();
    preview_.clear();
    preview_index_ = maximum_entries;

    capacity_bytes_ = 0;
    used_bytes_     = 0;
    struct statvfs storage{};
    if (statvfs(root_path_.c_str(), &storage) == 0) {
        const std::uint64_t blocks      = static_cast<std::uint64_t>(storage.f_blocks);
        const std::uint64_t free_blocks = std::min(blocks, static_cast<std::uint64_t>(storage.f_bfree));
        const std::uint64_t block_size  = static_cast<std::uint64_t>(storage.f_frsize);
        capacity_bytes_                 = saturatingProduct(blocks, block_size);
        used_bytes_                     = saturatingProduct(blocks - free_blocks, block_size);
    }

    if (current_path_ == root_path_) {
        display_path_ = "~/";
    } else {
        std::size_t offset = root_path_.size();
        if (offset < current_path_.size() && current_path_[offset] == '/') {
            ++offset;
        }
        display_path_ = current_path_.substr(offset);
    }
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

FilesCommandResult FilesModel::unsupported(FilesAction action) const
{
    return FilesCommandResult::failure(FilesCommandStatus::Unsupported, std::string(actionCapability(action).reason));
}

void FilesModel::updateContentScreen() noexcept
{
    content_screen_ = current_path_ == root_path_ ? FilesScreen::Home : FilesScreen::Folder;
}

void FilesModel::resetSelection()
{
    selected_.assign(entries_.size(), false);
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
