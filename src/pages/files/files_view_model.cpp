#include "pages/files/files_view_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <limits>
#include <utility>

namespace {

std::string formatFileSize(std::uint64_t bytes)
{
    static constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value                         = static_cast<double>(bytes);
    std::size_t unit                     = 0;
    while (value >= 1024.0 && unit + 1U < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }

    char buffer[FilesModel::detail_capacity]{};
    if (unit == 0U) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    } else if (value >= 10.0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, units[unit]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
    }
    return buffer;
}

std::string formatItemCount(std::size_t count)
{
    char buffer[FilesModel::detail_capacity]{};
    std::snprintf(buffer, sizeof(buffer), "%zu %s", count, count == 1U ? "item" : "items");
    return buffer;
}

// The capitalized count used in menu titles, such as "2 Items".
std::string formatItemTitle(std::size_t count)
{
    char buffer[FilesModel::detail_capacity]{};
    std::snprintf(buffer, sizeof(buffer), "%zu %s", count, count == 1U ? "Item" : "Items");
    return buffer;
}

std::string quoted(const std::string &name)
{
    return "\u201c" + name + "\u201d";
}

std::string formatModifiedDate(std::int64_t seconds)
{
    const auto time = static_cast<std::time_t>(seconds);
    std::tm local{};
    if (localtime_r(&time, &local) == nullptr) return {};

    char buffer[FilesModel::detail_capacity]{};
    std::snprintf(buffer, sizeof(buffer), "%d/%d/%d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buffer;
}

// Date and time for Get Info, such as "2026/5/4 22:18".
std::string formatDateTime(std::int64_t seconds)
{
    const auto time = static_cast<std::time_t>(seconds);
    std::tm local{};
    if (localtime_r(&time, &local) == nullptr) return "--";

    char buffer[FilesModel::detail_capacity]{};
    std::snprintf(buffer, sizeof(buffer), "%d/%d/%d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min);
    return buffer;
}

// Exact byte count with thousands separators, such as "2,490,492 bytes".
std::string formatByteCount(std::uint64_t bytes)
{
    std::string digits = std::to_string(bytes);
    for (std::size_t position = digits.size(); position > 3U; position -= 3U) digits.insert(position - 3U, ",");
    return digits + (bytes == 1U ? " byte" : " bytes");
}

// Short uppercase extension printed on document icons, such as "TXT" or "PDF".
std::string typeLabelOf(std::string_view name)
{
    static constexpr std::size_t maximum_length = 4;
    const std::size_t dot                       = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0U) return {};

    const std::string_view extension = name.substr(dot + 1U);
    if (extension.empty() || extension.size() > maximum_length) return {};
    std::string label;
    for (const char character : extension) {
        if (!std::isalnum(static_cast<unsigned char>(character))) return {};
        label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return label;
}

std::string describeKind(const FilesItemInfo &info, const std::string &type_label)
{
    if (info.is_symlink) return "Symbolic Link";
    switch (info.kind) {
        case FilesEntryKind::Folder:
            return "Folder";
        case FilesEntryKind::Music:
            return type_label.empty() ? "Audio" : type_label + " Audio";
        case FilesEntryKind::Pdf:
            return "PDF Document";
        case FilesEntryKind::Text:
            return type_label.empty() || type_label == "TXT" ? "Plain Text" : type_label + " Text";
        case FilesEntryKind::Binary:
            break;
    }
    return type_label.empty() ? "Document" : type_label + " File";
}

std::int32_t toSubjectCount(std::size_t count)
{
    constexpr std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::min(count, maximum));
}

std::int32_t toSubjectMask(std::uint32_t mask)
{
    constexpr std::uint32_t maximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::min(mask, maximum));
}

}  // namespace

FilesViewModel::FilesViewModel(std::string_view root_path)
    : model_(root_path),
      screen_(static_cast<std::int32_t>(model_.screen()), static_cast<std::int32_t>(FilesScreen::Home),
              static_cast<std::int32_t>(FilesScreen::SelectionActions)),
      content_screen_(static_cast<std::int32_t>(model_.contentScreen()), static_cast<std::int32_t>(FilesScreen::Home),
                      static_cast<std::int32_t>(FilesScreen::Folder)),
      content_revision_(0, 0),
      entry_count_(0, 0, static_cast<std::int32_t>(FilesModel::maximum_entries)),
      search_count_(0, 0, static_cast<std::int32_t>(FilesModel::maximum_entries)),
      selected_count_(0, 0, static_cast<std::int32_t>(FilesModel::maximum_entries)),
      supported_actions_(0, 0),
      enabled_actions_(0, 0),
      sort_key_(static_cast<std::int32_t>(model_.sortKey()), static_cast<std::int32_t>(FilesSortKey::Name),
                static_cast<std::int32_t>(FilesSortKey::Size)),
      sort_ascending_(model_.sortAscending() ? 1 : 0, 0, 1),
      view_mode_(static_cast<std::int32_t>(model_.viewMode()), static_cast<std::int32_t>(FilesViewMode::Icons),
                 static_cast<std::int32_t>(FilesViewMode::List)),
      query_(model_.query()),
      folder_name_(model_.folderName()),
      rename_name_(model_.renameName()),
      preview_(model_.preview()),
      display_path_(model_.displayPath()),
      directory_name_subject_(model_.directoryName()),
      error_(model_.error())
{
    error_text_ = model_.error();
    publish();
}

bool FilesViewModel::initialized() const noexcept
{
    return model_.initialized();
}

FilesScreen FilesViewModel::screen() const noexcept
{
    return model_.screen();
}

FilesScreen FilesViewModel::content_screen() const noexcept
{
    return model_.contentScreen();
}

const std::string &FilesViewModel::query() const noexcept
{
    return model_.query();
}

const std::string &FilesViewModel::folder_name() const noexcept
{
    return model_.folderName();
}

const std::string &FilesViewModel::preview() const noexcept
{
    return model_.preview();
}

const std::string &FilesViewModel::display_path() const noexcept
{
    return model_.displayPath();
}

const std::string &FilesViewModel::directory_name() const noexcept
{
    return directory_name_;
}

const std::string &FilesViewModel::error_text() const noexcept
{
    return error_text_;
}

std::size_t FilesViewModel::entry_count() const noexcept
{
    return entries_.size();
}

const FilesEntryPresentation *FilesViewModel::entry(std::size_t index) const noexcept
{
    return index < entries_.size() ? &entries_[index] : nullptr;
}

std::size_t FilesViewModel::search_count() const noexcept
{
    return search_entries_.size();
}

const FilesEntryPresentation *FilesViewModel::search_entry(std::size_t index) const noexcept
{
    return index < search_entries_.size() ? &search_entries_[index] : nullptr;
}

const FilesEntryPresentation *FilesViewModel::preview_entry() const noexcept
{
    return has_preview_entry_ ? &preview_entry_ : nullptr;
}

const FilesEntryPresentation *FilesViewModel::action_entry() const noexcept
{
    return has_action_entry_ ? &action_entry_ : nullptr;
}

std::size_t FilesViewModel::action_index() const noexcept
{
    return model_.actionIndex();
}

std::size_t FilesViewModel::action_count() const noexcept
{
    return model_.actionCount();
}

bool FilesViewModel::selecting() const noexcept
{
    return model_.selecting();
}

const std::string &FilesViewModel::rename_name() const noexcept
{
    return model_.renameName();
}

const FilesInfoPresentation *FilesViewModel::info() const noexcept
{
    return has_info_ ? &info_ : nullptr;
}

bool FilesViewModel::has_clipboard() const noexcept
{
    return model_.hasClipboard();
}

const std::string &FilesViewModel::move_title() const noexcept
{
    return move_title_;
}

const std::string &FilesViewModel::move_display_path() const noexcept
{
    return model_.moveDisplayPath();
}

const std::string &FilesViewModel::move_parent_name() const noexcept
{
    return move_parent_name_;
}

bool FilesViewModel::move_at_root() const noexcept
{
    return model_.moveAtRoot();
}

std::size_t FilesViewModel::move_destination_count() const noexcept
{
    return move_entries_.size();
}

const FilesEntryPresentation *FilesViewModel::move_destination(std::size_t index) const noexcept
{
    return index < move_entries_.size() ? &move_entries_[index] : nullptr;
}

bool FilesViewModel::move_destination_enabled(std::size_t index) const noexcept
{
    return index < move_enabled_.size() && move_enabled_[index];
}

bool FilesViewModel::can_move_here() const noexcept
{
    return can_move_here_;
}

bool FilesViewModel::is_selected(std::size_t index) const noexcept
{
    return model_.isSelected(index);
}

std::size_t FilesViewModel::selected_count() const noexcept
{
    return model_.selectedCount();
}

bool FilesViewModel::all_selected() const noexcept
{
    return !entries_.empty() && model_.selectedCount() == entries_.size();
}

std::string FilesViewModel::selection_title() const
{
    const std::size_t count = model_.selectedCount();
    if (count == 0U) return "Select Items";
    char buffer[FilesModel::detail_capacity]{};
    std::snprintf(buffer, sizeof(buffer), "%zu Selected", count);
    return buffer;
}

std::string FilesViewModel::copy_items_title() const
{
    return "Copy " + formatItemTitle(model_.actionCount());
}

std::string FilesViewModel::new_folder_with_items_title() const
{
    return "New Folder with " + formatItemTitle(model_.actionCount());
}

std::string FilesViewModel::move_hint() const
{
    const std::size_t count = model_.actionCount();
    if (count == 1U && has_action_entry_) return "Choose a folder for " + quoted(action_entry_.name) + ".";
    return "Choose a folder for " + formatItemCount(count) + ".";
}

std::string FilesViewModel::delete_title() const
{
    const std::size_t count = model_.actionCount();
    if (count == 1U && has_action_entry_) return "Delete " + quoted(action_entry_.name) + "?";
    return "Delete " + formatItemTitle(count) + "?";
}

std::string FilesViewModel::delete_message() const
{
    return model_.actionCount() == 1U ? "This item will be deleted immediately. You can\u2019t undo this action."
                                      : "These items will be deleted immediately. You can\u2019t undo this action.";
}

FilesSortKey FilesViewModel::sort_key() const noexcept
{
    return model_.sortKey();
}

bool FilesViewModel::sort_ascending() const noexcept
{
    return model_.sortAscending();
}

FilesViewMode FilesViewModel::view_mode() const noexcept
{
    return model_.viewMode();
}

FilesActionCapability FilesViewModel::action_capability(FilesAction action) const noexcept
{
    return model_.actionCapability(action);
}

std::uint32_t FilesViewModel::supported_action_mask() const noexcept
{
    return model_.supportedActionMask();
}

std::uint32_t FilesViewModel::enabled_action_mask() const noexcept
{
    return model_.enabledActionMask();
}

FilesCommandResult FilesViewModel::show(FilesScreen screen)
{
    FilesCommandResult result = model_.show(screen);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::back()
{
    FilesCommandResult result = model_.back();
    record(result);
    publish();
    return result;
}

void FilesViewModel::set_query(std::string_view query)
{
    model_.setQuery(query);
    error_text_.clear();
    publish();
}

void FilesViewModel::set_folder_name(std::string_view name)
{
    model_.setFolderName(name);
    error_text_.clear();
    publish();
}

void FilesViewModel::set_rename_name(std::string_view name)
{
    model_.setRenameName(name);
    error_text_.clear();
    publish();
}

FilesCommandResult FilesViewModel::open_entry(std::size_t index)
{
    FilesCommandResult result = model_.openEntry(index);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::open_search_entry(std::size_t index)
{
    FilesCommandResult result = model_.openSearchEntry(index);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::activate_entry(std::size_t index)
{
    FilesCommandResult result = model_.activateEntry(index);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::set_sort(FilesSortKey key)
{
    FilesCommandResult result = model_.setSortKey(key);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::set_view_mode(FilesViewMode mode)
{
    FilesCommandResult result = model_.setViewMode(mode);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::create_folder()
{
    FilesCommandResult result = model_.createFolder();
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::toggle_selection(std::size_t index)
{
    FilesCommandResult result = model_.toggleSelection(index)
                                    ? FilesCommandResult::success()
                                    : FilesCommandResult::failure(FilesCommandStatus::NotFound, "File item not found");
    record(result);
    publish();
    return result;
}

void FilesViewModel::select_all()
{
    model_.selectAll();
    error_text_.clear();
    publish();
}

void FilesViewModel::clear_selection()
{
    model_.clearSelection();
    error_text_.clear();
    publish();
}

void FilesViewModel::toggle_select_all()
{
    model_.toggleSelectAll();
    error_text_.clear();
    publish();
}

FilesCommandResult FilesViewModel::show_actions(std::size_t index)
{
    return run(model_.showActions(index));
}

FilesCommandResult FilesViewModel::open_action_entry()
{
    return run(model_.openActionEntry());
}

FilesCommandResult FilesViewModel::rename_action_entry()
{
    return run(model_.renameActionEntry());
}

FilesCommandResult FilesViewModel::paste()
{
    return run(model_.paste());
}

FilesCommandResult FilesViewModel::copy_action_items()
{
    return run(model_.copyActionItems());
}

FilesCommandResult FilesViewModel::compress_action_items()
{
    return run(model_.compressActionItems());
}

FilesCommandResult FilesViewModel::duplicate_action_items()
{
    return run(model_.duplicateActionItems());
}

FilesCommandResult FilesViewModel::new_folder_with_action_items()
{
    return run(model_.newFolderWithActionItems());
}

FilesCommandResult FilesViewModel::remove_action_items()
{
    return run(model_.removeActionItems());
}

FilesCommandResult FilesViewModel::open_move_destination(std::size_t index)
{
    return run(model_.openMoveDestination(index));
}

FilesCommandResult FilesViewModel::move_destination_up()
{
    return run(model_.moveDestinationUp());
}

FilesCommandResult FilesViewModel::move_action_items()
{
    return run(model_.moveActionItems());
}

lv_subject_t *FilesViewModel::screen_subject() noexcept
{
    return screen_.get();
}

lv_subject_t *FilesViewModel::content_screen_subject() noexcept
{
    return content_screen_.get();
}

lv_subject_t *FilesViewModel::content_revision_subject() noexcept
{
    return content_revision_.get();
}

lv_subject_t *FilesViewModel::entry_count_subject() noexcept
{
    return entry_count_.get();
}

lv_subject_t *FilesViewModel::search_count_subject() noexcept
{
    return search_count_.get();
}

lv_subject_t *FilesViewModel::selected_count_subject() noexcept
{
    return selected_count_.get();
}

lv_subject_t *FilesViewModel::supported_actions_subject() noexcept
{
    return supported_actions_.get();
}

lv_subject_t *FilesViewModel::enabled_actions_subject() noexcept
{
    return enabled_actions_.get();
}

lv_subject_t *FilesViewModel::sort_key_subject() noexcept
{
    return sort_key_.get();
}

lv_subject_t *FilesViewModel::sort_ascending_subject() noexcept
{
    return sort_ascending_.get();
}

lv_subject_t *FilesViewModel::view_mode_subject() noexcept
{
    return view_mode_.get();
}

lv_subject_t *FilesViewModel::query_subject() noexcept
{
    return query_.get();
}

lv_subject_t *FilesViewModel::folder_name_subject() noexcept
{
    return folder_name_.get();
}

lv_subject_t *FilesViewModel::rename_name_subject() noexcept
{
    return rename_name_.get();
}

lv_subject_t *FilesViewModel::preview_subject() noexcept
{
    return preview_.get();
}

lv_subject_t *FilesViewModel::display_path_subject() noexcept
{
    return display_path_.get();
}

lv_subject_t *FilesViewModel::directory_name_subject() noexcept
{
    return directory_name_subject_.get();
}

lv_subject_t *FilesViewModel::error_subject() noexcept
{
    return error_.get();
}

FilesEntryPresentation FilesViewModel::present(const FilesEntry &entry)
{
    FilesEntryPresentation presentation;
    presentation.name         = entry.name;
    presentation.kind         = entry.kind;
    presentation.is_directory = entry.is_directory;
    presentation.modified     = formatModifiedDate(entry.modified_time);
    if (entry.is_directory) {
        presentation.detail = entry.item_count ? formatItemCount(*entry.item_count) : "Folder";
    } else {
        presentation.detail     = entry.is_symlink ? "Symbolic link" : formatFileSize(entry.size);
        presentation.type_label = typeLabelOf(entry.name);
    }
    // One-line summary used by list rows and the preview header, such as "2026/9/29 - 1.7 MB".
    presentation.summary =
        presentation.modified.empty() ? presentation.detail : presentation.modified + " - " + presentation.detail;
    return presentation;
}

FilesInfoPresentation FilesViewModel::present(const FilesItemInfo &info)
{
    FilesInfoPresentation presentation;
    presentation.name       = info.name;
    presentation.icon_kind  = info.kind;
    presentation.type_label = info.is_directory ? std::string() : typeLabelOf(info.name);
    presentation.kind       = describeKind(info, presentation.type_label);
    presentation.can_open   = !info.is_symlink;
    // "2,490,492 bytes (2.4 MB on disk), 12 items", like the iOS Files information list.
    presentation.size = formatByteCount(info.size) + " (" + formatFileSize(info.disk_size) + " on disk)";
    if (info.item_count) presentation.size += ", " + formatItemCount(*info.item_count);
    if (!info.complete) presentation.size = "At least " + presentation.size;
    presentation.created  = info.created_time ? formatDateTime(*info.created_time) : "--";
    presentation.modified = formatDateTime(info.modified_time);
    presentation.location = info.location;
    return presentation;
}

FilesCommandResult FilesViewModel::run(FilesCommandResult result)
{
    record(result);
    publish();
    return result;
}

void FilesViewModel::record(const FilesCommandResult &result)
{
    error_text_ = result.succeeded() ? model_.error() : result.message();
}

void FilesViewModel::refreshPresentation()
{
    entries_.clear();
    entries_.reserve(model_.entryCount());
    for (std::size_t index = 0; index < model_.entryCount(); ++index) {
        const FilesEntry *source = model_.entry(index);
        if (source != nullptr) entries_.push_back(present(*source));
    }

    search_entries_.clear();
    search_entries_.reserve(model_.searchCount());
    for (std::size_t index = 0; index < model_.searchCount(); ++index) {
        const FilesEntry *source = model_.searchEntry(index);
        if (source != nullptr) search_entries_.push_back(present(*source));
    }

    move_entries_.clear();
    move_enabled_.clear();
    for (std::size_t index = 0; index < model_.moveDestinationCount(); ++index) {
        const FilesEntry *source = model_.moveDestination(index);
        if (source == nullptr) continue;
        move_entries_.push_back(present(*source));
        move_enabled_.push_back(model_.moveDestinationEnabled(index));
    }
    can_move_here_    = model_.canMoveHere();
    move_title_       = model_.moveDirectoryName();
    move_parent_name_ = model_.moveParentName();

    const FilesEntry *source_preview = model_.previewEntry();
    has_preview_entry_               = source_preview != nullptr;
    preview_entry_                   = has_preview_entry_ ? present(*source_preview) : FilesEntryPresentation{};
    const FilesEntry *source_action  = model_.actionEntry();
    has_action_entry_                = source_action != nullptr;
    action_entry_                    = has_action_entry_ ? present(*source_action) : FilesEntryPresentation{};
    const FilesItemInfo *source_info = model_.info();
    has_info_                        = source_info != nullptr;
    info_                            = has_info_ ? present(*source_info) : FilesInfoPresentation{};
    directory_name_                  = model_.directoryName();
}

void FilesViewModel::publish()
{
    refreshPresentation();

    screen_.set(static_cast<std::int32_t>(model_.screen()));
    content_screen_.set(static_cast<std::int32_t>(model_.contentScreen()));
    entry_count_.set(toSubjectCount(entries_.size()));
    search_count_.set(toSubjectCount(search_entries_.size()));
    selected_count_.set(toSubjectCount(model_.selectedCount()));
    supported_actions_.set(toSubjectMask(model_.supportedActionMask()));
    enabled_actions_.set(toSubjectMask(model_.enabledActionMask()));
    sort_key_.set(static_cast<std::int32_t>(model_.sortKey()));
    sort_ascending_.set(model_.sortAscending() ? 1 : 0);
    view_mode_.set(static_cast<std::int32_t>(model_.viewMode()));
    query_.set(model_.query());
    folder_name_.set(model_.folderName());
    rename_name_.set(model_.renameName());
    preview_.set(model_.preview());
    display_path_.set(model_.displayPath());
    directory_name_subject_.set(directory_name_);
    error_.set(error_text_);

    if (revision_ == std::numeric_limits<std::int32_t>::max()) {
        revision_ = 0;
    } else {
        ++revision_;
    }
    content_revision_.set(revision_);
}
