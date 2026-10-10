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
    std::string modified;
    std::string summary;
    std::string type_label;
    FilesEntryKind kind = FilesEntryKind::Binary;
    bool is_directory   = false;
};

// The Get Info page: the item's icon, name and kind, then one value per information row.
struct FilesInfoPresentation {
    std::string name;
    std::string type_label;
    std::string kind;
    std::string size;
    std::string created;
    std::string modified;
    std::string location;
    FilesEntryKind icon_kind = FilesEntryKind::Binary;
    bool can_open            = false;
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

    [[nodiscard]] std::size_t entry_count() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *entry(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t search_count() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *search_entry(std::size_t index) const noexcept;
    [[nodiscard]] const FilesEntryPresentation *preview_entry() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *action_entry() const noexcept;
    [[nodiscard]] std::size_t action_index() const noexcept;
    [[nodiscard]] std::size_t action_count() const noexcept;
    [[nodiscard]] bool selecting() const noexcept;
    [[nodiscard]] const std::string &rename_name() const noexcept;
    [[nodiscard]] const FilesInfoPresentation *info() const noexcept;
    [[nodiscard]] bool has_clipboard() const noexcept;
    [[nodiscard]] const std::string &move_title() const noexcept;
    [[nodiscard]] const std::string &move_display_path() const noexcept;
    [[nodiscard]] const std::string &move_parent_name() const noexcept;
    [[nodiscard]] bool move_at_root() const noexcept;
    [[nodiscard]] std::size_t move_destination_count() const noexcept;
    [[nodiscard]] const FilesEntryPresentation *move_destination(std::size_t index) const noexcept;
    [[nodiscard]] bool move_destination_enabled(std::size_t index) const noexcept;
    [[nodiscard]] bool can_move_here() const noexcept;
    [[nodiscard]] bool is_selected(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t selected_count() const noexcept;
    [[nodiscard]] bool all_selected() const noexcept;
    [[nodiscard]] std::string selection_title() const;
    // Titles and messages that name the action items: one by name, several by their number.
    [[nodiscard]] std::string copy_items_title() const;
    [[nodiscard]] std::string new_folder_with_items_title() const;
    [[nodiscard]] std::string move_hint() const;
    [[nodiscard]] std::string delete_title() const;
    [[nodiscard]] std::string delete_message() const;
    [[nodiscard]] FilesSortKey sort_key() const noexcept;
    [[nodiscard]] bool sort_ascending() const noexcept;
    [[nodiscard]] FilesViewMode view_mode() const noexcept;

    [[nodiscard]] FilesActionCapability action_capability(FilesAction action) const noexcept;
    [[nodiscard]] std::uint32_t supported_action_mask() const noexcept;
    [[nodiscard]] std::uint32_t enabled_action_mask() const noexcept;

    FilesCommandResult show(FilesScreen screen);
    FilesCommandResult back();
    void set_query(std::string_view query);
    void set_folder_name(std::string_view name);
    void set_rename_name(std::string_view name);
    FilesCommandResult open_entry(std::size_t index);
    FilesCommandResult open_search_entry(std::size_t index);
    FilesCommandResult activate_entry(std::size_t index);
    FilesCommandResult create_folder();
    FilesCommandResult set_sort(FilesSortKey key);
    FilesCommandResult set_view_mode(FilesViewMode mode);
    FilesCommandResult toggle_selection(std::size_t index);
    void select_all();
    void clear_selection();
    void toggle_select_all();

    FilesCommandResult show_actions(std::size_t index);
    FilesCommandResult open_action_entry();
    FilesCommandResult rename_action_entry();
    FilesCommandResult paste();
    FilesCommandResult copy_action_items();
    FilesCommandResult compress_action_items();
    FilesCommandResult duplicate_action_items();
    FilesCommandResult new_folder_with_action_items();
    FilesCommandResult remove_action_items();
    FilesCommandResult open_move_destination(std::size_t index);
    FilesCommandResult move_destination_up();
    FilesCommandResult move_action_items();

    [[nodiscard]] lv_subject_t *screen_subject() noexcept;
    [[nodiscard]] lv_subject_t *content_screen_subject() noexcept;
    [[nodiscard]] lv_subject_t *content_revision_subject() noexcept;
    [[nodiscard]] lv_subject_t *entry_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *search_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *selected_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *supported_actions_subject() noexcept;
    [[nodiscard]] lv_subject_t *enabled_actions_subject() noexcept;
    [[nodiscard]] lv_subject_t *sort_key_subject() noexcept;
    [[nodiscard]] lv_subject_t *sort_ascending_subject() noexcept;
    [[nodiscard]] lv_subject_t *view_mode_subject() noexcept;
    [[nodiscard]] lv_subject_t *query_subject() noexcept;
    [[nodiscard]] lv_subject_t *folder_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *rename_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *preview_subject() noexcept;
    [[nodiscard]] lv_subject_t *display_path_subject() noexcept;
    [[nodiscard]] lv_subject_t *directory_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *error_subject() noexcept;

private:
    static FilesEntryPresentation present(const FilesEntry &entry);
    static FilesInfoPresentation present(const FilesItemInfo &info);

    FilesCommandResult run(FilesCommandResult result);
    void record(const FilesCommandResult &result);
    void refreshPresentation();
    void publish();

    FilesModel model_;
    std::vector<FilesEntryPresentation> entries_;
    std::vector<FilesEntryPresentation> search_entries_;
    std::vector<FilesEntryPresentation> move_entries_;
    std::vector<bool> move_enabled_;
    FilesEntryPresentation preview_entry_;
    bool has_preview_entry_ = false;
    FilesEntryPresentation action_entry_;
    bool has_action_entry_ = false;
    FilesInfoPresentation info_;
    bool has_info_      = false;
    bool can_move_here_ = false;
    std::string move_title_;
    std::string move_parent_name_;
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
    IntSubject sort_key_;
    IntSubject sort_ascending_;
    IntSubject view_mode_;
    StringSubject<FilesModel::text_capacity> query_;
    StringSubject<FilesModel::text_capacity> folder_name_;
    StringSubject<FilesModel::name_capacity> rename_name_;
    StringSubject<FilesModel::preview_capacity> preview_;
    StringSubject<FilesModel::path_capacity> display_path_;
    StringSubject<FilesModel::name_capacity> directory_name_subject_;
    StringSubject<FilesModel::name_capacity> error_;
};

#endif
