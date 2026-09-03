#include "pages/files/files_view_model.hpp"

#include <algorithm>
#include <cstdio>
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

std::string formatStorageSize(std::uint64_t bytes)
{
    constexpr double bytes_per_gibibyte = 1024.0 * 1024.0 * 1024.0;
    const double gibibytes              = static_cast<double>(bytes) / bytes_per_gibibyte;
    char buffer[32]{};
    if (gibibytes >= 10.0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f GB", gibibytes);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB", gibibytes);
    }
    return buffer;
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
              static_cast<std::int32_t>(FilesScreen::More)),
      content_screen_(static_cast<std::int32_t>(model_.contentScreen()), static_cast<std::int32_t>(FilesScreen::Home),
                      static_cast<std::int32_t>(FilesScreen::Folder)),
      content_revision_(0, 0),
      entry_count_(0, 0, static_cast<std::int32_t>(FilesModel::maximum_entries)),
      search_count_(0, 0, static_cast<std::int32_t>(FilesModel::maximum_entries)),
      selected_count_(0, 0, static_cast<std::int32_t>(FilesModel::maximum_entries)),
      supported_actions_(0, 0),
      enabled_actions_(0, 0),
      storage_percent_(0, 0, 100),
      query_(model_.query()),
      folder_name_(model_.folderName()),
      preview_(model_.preview()),
      display_path_(model_.displayPath()),
      directory_name_subject_(model_.directoryName()),
      error_(model_.error()),
      storage_summary_()
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

const FilesStoragePresentation &FilesViewModel::storage() const noexcept
{
    return storage_;
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

std::size_t FilesViewModel::destination_count() const noexcept
{
    return destination_entries_.size();
}

const FilesEntryPresentation *FilesViewModel::destination_entry(std::size_t index) const noexcept
{
    return index < destination_entries_.size() ? &destination_entries_[index] : nullptr;
}

const FilesEntryPresentation *FilesViewModel::preview_entry() const noexcept
{
    return has_preview_entry_ ? &preview_entry_ : nullptr;
}

bool FilesViewModel::is_selected(std::size_t index) const noexcept
{
    return model_.isSelected(index);
}

std::size_t FilesViewModel::selected_count() const noexcept
{
    return model_.selectedCount();
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

FilesCommandResult FilesViewModel::copy_selection()
{
    FilesCommandResult result = model_.copySelection();
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::rename_selection(std::string_view new_name)
{
    FilesCommandResult result = model_.renameSelection(new_name);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::move_selection_to(std::size_t destination_index)
{
    FilesCommandResult result = model_.moveSelectionTo(destination_index);
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::archive_selection()
{
    FilesCommandResult result = model_.archiveSelection();
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::remove_selection()
{
    FilesCommandResult result = model_.removeSelection();
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::change_sort()
{
    FilesCommandResult result = model_.changeSort();
    record(result);
    publish();
    return result;
}

FilesCommandResult FilesViewModel::change_view()
{
    FilesCommandResult result = model_.changeView();
    record(result);
    publish();
    return result;
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

lv_subject_t *FilesViewModel::storage_percent_subject() noexcept
{
    return storage_percent_.get();
}

lv_subject_t *FilesViewModel::query_subject() noexcept
{
    return query_.get();
}

lv_subject_t *FilesViewModel::folder_name_subject() noexcept
{
    return folder_name_.get();
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

lv_subject_t *FilesViewModel::storage_summary_subject() noexcept
{
    return storage_summary_.get();
}

FilesEntryPresentation FilesViewModel::present(const FilesEntry &entry)
{
    FilesEntryPresentation presentation;
    presentation.name         = entry.name;
    presentation.kind         = entry.kind;
    presentation.is_directory = entry.is_directory;
    if (entry.is_directory) {
        presentation.detail = "Folder";
    } else if (entry.is_symlink) {
        presentation.detail = "Symbolic link";
    } else {
        presentation.detail = formatFileSize(entry.size);
    }
    return presentation;
}

FilesStoragePresentation FilesViewModel::presentStorage(std::uint64_t capacity, std::uint64_t used)
{
    FilesStoragePresentation presentation;
    presentation.capacity_bytes = capacity;
    presentation.used_bytes     = used;
    if (capacity == 0U) {
        presentation.used_percent = 0;
    } else if (used >= capacity) {
        presentation.used_percent = 100;
    } else {
        const long double ratio   = static_cast<long double>(used) / static_cast<long double>(capacity);
        presentation.used_percent = static_cast<std::int32_t>(ratio * 100.0L);
    }
    presentation.used_label     = formatStorageSize(used);
    presentation.capacity_label = formatStorageSize(capacity);
    presentation.summary        = presentation.used_label + " used of " + presentation.capacity_label;
    return presentation;
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

    destination_entries_.clear();
    destination_entries_.reserve(model_.destinationCount());
    for (std::size_t index = 0; index < model_.destinationCount(); ++index) {
        const FilesEntry *source = model_.destinationEntry(index);
        if (source != nullptr) {
            destination_entries_.push_back(present(*source));
        }
    }

    const FilesEntry *source_preview = model_.previewEntry();
    has_preview_entry_               = source_preview != nullptr;
    preview_entry_                   = has_preview_entry_ ? present(*source_preview) : FilesEntryPresentation{};
    storage_                         = presentStorage(model_.capacityBytes(), model_.usedBytes());
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
    storage_percent_.set(storage_.used_percent);
    query_.set(model_.query());
    folder_name_.set(model_.folderName());
    preview_.set(model_.preview());
    display_path_.set(model_.displayPath());
    directory_name_subject_.set(directory_name_);
    error_.set(error_text_);
    storage_summary_.set(storage_.summary);

    if (revision_ == std::numeric_limits<std::int32_t>::max()) {
        revision_ = 0;
    } else {
        ++revision_;
    }
    content_revision_.set(revision_);
}
