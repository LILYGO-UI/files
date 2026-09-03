#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_VIEW_MODEL_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "pages/files/files_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct FilesEntryPresentation {
    std::string name;
    std::string detail;
    FilesEntryKind kind = FilesEntryKind::Binary;
    bool is_directory   = false;
};

struct FilesStoragePresentation {
    std::uint64_t capacity_bytes = 0;
    std::uint64_t used_bytes     = 0;
    std::int32_t used_percent    = 0;
    std::string used_label;
    std::string capacity_label;
    std::string summary;
};

class FilesViewModel {
public:
    static constexpr std::size_t text_capacity   = FilesModel::text_capacity;
    static constexpr std::size_t maximum_entries = FilesModel::maximum_entries;

    explicit FilesViewModel(std::string_view root_path);

    FilesViewModel(const FilesViewModel &)            = delete;
    FilesViewModel &operator=(const FilesViewModel &) = delete;
    FilesViewModel(FilesViewModel &&)                 = delete;
    FilesViewModel &operator=(FilesViewModel &&)      = delete;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] FilesScreen screen() const noexcept;
    [[nodiscard]] FilesScreen content_screen() const noexcept;
    [[nodiscard]] const std::string &query() const noexcept;
    [[nodiscard]] const std::string &folder_name() const noexcept;
    [[nodiscard]] const std::string &preview() const noexcept;
    [[nodiscard]] const std::string &display_path() const noexcept;
    [[nodiscard]] const std::string &directory_name() const noexcept;
    [[nodiscard]] const std::string &error_text() const noexcept;
    [[nodiscard]] const FilesStoragePresentation &storage() const noexcept;

    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t search_count() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *search_entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t destination_count() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *destination_entry(std::size_t index) const noexcept;
    [[nodiscard]] const FilesEntryPresentation *preview_entry() const noexcept;
    [[nodiscard]] bool is_selected(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t selected_count() const noexcept;

    [[nodiscard]] FilesActionCapability action_capability(FilesAction action) const noexcept;
    [[nodiscard]] std::uint32_t supported_action_mask() const noexcept;
    [[nodiscard]] std::uint32_t enabled_action_mask() const noexcept;

    FilesCommandResult show(FilesScreen screen);
    FilesCommandResult back();
    void set_query(std::string_view query);
    void set_folder_name(std::string_view name);
    FilesCommandResult open_entry(std::size_t index);
    FilesCommandResult open_search_entry(std::size_t index);
    FilesCommandResult create_folder();
    FilesCommandResult toggle_selection(std::size_t index);
    void select_all();
    void clear_selection();

    FilesCommandResult copy_selection();
    FilesCommandResult rename_selection(std::string_view new_name);
    FilesCommandResult move_selection_to(std::size_t destination_index);
    FilesCommandResult archive_selection();
    FilesCommandResult remove_selection();
    FilesCommandResult change_sort();
    FilesCommandResult change_view();

    [[nodiscard]] lv_subject_t *screen_subject() noexcept;
    [[nodiscard]] lv_subject_t *content_screen_subject() noexcept;
    [[nodiscard]] lv_subject_t *content_revision_subject() noexcept;
    [[nodiscard]] lv_subject_t *entry_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *search_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *selected_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *supported_actions_subject() noexcept;
    [[nodiscard]] lv_subject_t *enabled_actions_subject() noexcept;
    [[nodiscard]] lv_subject_t *storage_percent_subject() noexcept;
    [[nodiscard]] lv_subject_t *query_subject() noexcept;
    [[nodiscard]] lv_subject_t *folder_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *preview_subject() noexcept;
    [[nodiscard]] lv_subject_t *display_path_subject() noexcept;
    [[nodiscard]] lv_subject_t *directory_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *error_subject() noexcept;
    [[nodiscard]] lv_subject_t *storage_summary_subject() noexcept;

private:
    static FilesEntryPresentation present(const FilesEntry &entry);
    static FilesStoragePresentation presentStorage(std::uint64_t capacity, std::uint64_t used);

    void record(const FilesCommandResult &result);
    void refreshPresentation();
    void publish();

    FilesModel model_;
    std::vector<FilesEntryPresentation> entries_;
    std::vector<FilesEntryPresentation> search_entries_;
    std::vector<FilesEntryPresentation> destination_entries_;
    FilesEntryPresentation preview_entry_;
    bool has_preview_entry_ = false;
    FilesStoragePresentation storage_;
    std::string directory_name_;
    std::string error_text_;
    std::int32_t revision_ = 0;

    IntSubject screen_;
    IntSubject content_screen_;
    IntSubject content_revision_;
    IntSubject entry_count_;
    IntSubject search_count_;
    IntSubject selected_count_;
    IntSubject supported_actions_;
    IntSubject enabled_actions_;
    IntSubject storage_percent_;
    StringSubject<FilesModel::text_capacity> query_;
    StringSubject<FilesModel::text_capacity> folder_name_;
    StringSubject<FilesModel::preview_capacity> preview_;
    StringSubject<FilesModel::path_capacity> display_path_;
    StringSubject<FilesModel::name_capacity> directory_name_subject_;
    StringSubject<FilesModel::name_capacity> error_;
    StringSubject<96> storage_summary_;
};

#endif
