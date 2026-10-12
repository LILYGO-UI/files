#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_VIEW_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_VIEW_HPP

#include "pages/files/files_view_model.hpp"

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <string>

using FilesHelpCallback = void (*)(void *user_data);

class FilesView {
public:
    explicit FilesView(FilesViewModel &view_model, FilesHelpCallback help_callback = nullptr,
                       void *help_user_data = nullptr) noexcept;
    ~FilesView() noexcept;

    FilesView(const FilesView &)            = delete;
    FilesView &operator=(const FilesView &) = delete;
    FilesView(FilesView &&)                 = delete;
    FilesView &operator=(FilesView &&)      = delete;

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible) noexcept;

private:
    enum class RowSource : std::int32_t {
        Directory,
        Search,
        MoveDestination,
    };

    struct RowBinding {
        FilesView *view   = nullptr;
        std::size_t index = 0;
        RowSource source  = RowSource::Directory;
    };

    struct MenuBinding {
        FilesView *view    = nullptr;
        std::int32_t value = 0;
    };

    static void content_changed(lv_observer_t *observer, lv_subject_t *subject);
    static void render_async(void *user_data);
    static void place_async(void *user_data);
    static void key_remap_timer_tick(lv_timer_t *timer);
    static void size_changed(lv_event_t *event);
    static void back_clicked(lv_event_t *event);
    static void more_clicked(lv_event_t *event);
    static void help_clicked(lv_event_t *event);
    static void search_clicked(lv_event_t *event);
    static void search_field_key_pressed(lv_event_t *event);
    static void row_clicked(lv_event_t *event);
    static void row_key_pressed(lv_event_t *event);
    static void row_long_pressed(lv_event_t *event);
    static void action_clicked(lv_event_t *event);
    static void paste_clicked(lv_event_t *event);
    static void info_open_clicked(lv_event_t *event);
    static void delete_confirm_clicked(lv_event_t *event);
    static void move_up_clicked(lv_event_t *event);
    static void move_confirm_clicked(lv_event_t *event);
    static void query_changed(lv_event_t *event);
    static void menu_key_pressed(lv_event_t *event);
    static void search_input_activated(lv_event_t *event);
    static void search_input_deactivated(lv_event_t *event);
    static void keyboard_done(lv_event_t *event);
    static void select_mode_clicked(lv_event_t *event);
    static void select_all_clicked(lv_event_t *event);
    static void selection_more_clicked(lv_event_t *event);
    static void view_mode_clicked(lv_event_t *event);
    static void sort_clicked(lv_event_t *event);
    static void new_folder_clicked(lv_event_t *event);
    static void name_changed(lv_event_t *event);
    static void name_confirm_clicked(lv_event_t *event);

    static void make_plain(lv_obj_t *object) noexcept;
    static lv_obj_t *create_box(lv_obj_t *parent);
    static lv_obj_t *create_label(lv_obj_t *parent, const char *text, std::uint32_t size, std::uint32_t color);
    static void limit_lines(lv_obj_t *label, std::uint32_t size, std::int32_t lines) noexcept;
    static void apply_selection_state(lv_obj_t *item, bool selected) noexcept;

    void schedule_render() noexcept;
    void schedule_placement() noexcept;
    void configure_key_remap(bool enabled) noexcept;
    static lv_key_t remap_key(lv_indev_t *indev, lv_key_t key);
    void render();
    [[nodiscard]] std::string content_key() const;
    void clear_page_references() noexcept;
    void apply_layout() noexcept;
    void apply_grid_layout(std::int32_t available_width) noexcept;
    void apply_chrome_placement(std::int32_t padding, std::int32_t body_height) noexcept;
    void place_action_menu(bool reveal_item) noexcept;
    [[nodiscard]] lv_obj_t *find_entry_item(std::size_t index) const noexcept;
    void register_padded(lv_obj_t *object) noexcept;

    lv_obj_t *create_base();
    lv_obj_t *create_scroller();
    lv_obj_t *create_navbar_row();
    lv_obj_t *create_nav_titles(lv_obj_t *bar, const char *title, const char *subtitle, const char *subtitle_name);
    lv_obj_t *create_navbar(const char *title, const char *subtitle, const char *subtitle_name, bool show_back,
                            bool show_more);
    lv_obj_t *create_circle_button(lv_obj_t *parent, const char *symbol, const char *name, lv_event_cb_t callback);
    lv_obj_t *create_capsule_button(lv_obj_t *parent, const char *text, const char *name, lv_event_cb_t callback);
    lv_obj_t *create_search_field(lv_obj_t *parent, const char *text, bool editable);
    lv_obj_t *create_entry_collection(lv_obj_t *parent, bool search_results, bool populate = true);
    lv_obj_t *create_entry_item(lv_obj_t *parent, const FilesEntryPresentation &entry, std::size_t index,
                                RowSource source);
    lv_obj_t *create_grid_cell(lv_obj_t *parent, const FilesEntryPresentation &entry, bool selecting);
    lv_obj_t *create_list_row(lv_obj_t *parent, const FilesEntryPresentation &entry, bool selecting);
    lv_obj_t *create_selection_indicator(lv_obj_t *parent);
    void create_selection_toolbar();
    lv_obj_t *create_toolbar_button(lv_obj_t *parent, const char *symbol, const char *name, std::int32_t action);
    void apply_toolbar_state() noexcept;
    lv_obj_t *create_menu_card(const char *name);
    void create_menu_error(lv_obj_t *menu);
    lv_obj_t *create_menu_item(lv_obj_t *menu, const char *text, const char *subtitle, const char *symbol, bool checked,
                               const char *name, lv_event_cb_t callback, void *user_data);
    lv_obj_t *create_action_item(lv_obj_t *menu, const char *text, const char *symbol, const char *name,
                                 std::int32_t action, bool destructive);
    lv_obj_t *create_action_tile(lv_obj_t *row, const char *text, const char *symbol, const char *name,
                                 std::int32_t action);
    void create_menu_divider(lv_obj_t *menu);
    lv_obj_t *create_dialog(const char *name);
    lv_obj_t *create_dialog_button(lv_obj_t *parent, const char *text, const char *name, std::uint32_t background,
                                   std::uint32_t foreground, lv_event_cb_t callback);
    lv_obj_t *create_keyboard(lv_obj_t *parent, const char *name, lv_obj_t *textarea, std::int32_t height);
    lv_obj_t *create_info_row(lv_obj_t *list, const char *key, const std::string &value, const char *name);
    MenuBinding *bind_menu(std::int32_t value) noexcept;
    void create_divider(lv_obj_t *parent);
    void create_directory_content(lv_obj_t *scroll);
    void focus_first_entry() noexcept;
    void focus_overlay_control() noexcept;
    void create_underlay();
    void create_scrim(lv_opa_t opacity);
    void refresh_selection() noexcept;

    void build_home();
    void build_folder();
    void build_select();
    void build_selection_actions();
    void build_search();
    void build_preview();
    void build_more();
    void build_actions();
    void build_info();
    void build_move();
    void build_name_dialog();
    void build_delete();
    void refresh_search_results();

    FilesViewModel &view_model_;
    FilesHelpCallback help_callback_ = nullptr;
    void *help_user_data_            = nullptr;
    lv_obj_t *surface_          = nullptr;
    lv_obj_t *frame_            = nullptr;
    lv_obj_t *page_             = nullptr;
    lv_obj_t *base_             = nullptr;
    lv_obj_t *body_             = nullptr;
    lv_obj_t *chrome_           = nullptr;
    lv_obj_t *scroller_         = nullptr;
    lv_obj_t *entry_grid_       = nullptr;
    lv_obj_t *overlay_          = nullptr;
    lv_obj_t *action_preview_   = nullptr;
    lv_obj_t *nav_title_        = nullptr;
    lv_obj_t *select_all_label_ = nullptr;
    lv_obj_t *selection_error_  = nullptr;
    lv_obj_t *toolbar_          = nullptr;
    lv_obj_t *search_input_     = nullptr;
    lv_obj_t *search_count_     = nullptr;
    lv_obj_t *search_results_   = nullptr;
    lv_obj_t *search_error_     = nullptr;
    lv_obj_t *name_input_       = nullptr;
    lv_obj_t *name_hint_        = nullptr;
    lv_obj_t *keyboard_         = nullptr;
    std::array<lv_obj_t *, 4> padded_{};
    std::size_t padded_count_ = 0;
    std::array<RowBinding, FilesViewModel::maximum_entries> row_bindings_{};
    std::size_t row_binding_count_ = 0;
    std::array<MenuBinding, 12> menu_bindings_{};
    std::size_t menu_binding_count_ = 0;
    std::string render_key_;
    FilesScreen rendered_screen_ = FilesScreen::Home;
    // Where the directory content last shown was scrolled to, kept until the same content returns.
    std::string scroll_key_;
    std::int32_t scroll_y_       = 0;
    std::int32_t cell_width_     = 0;
    std::int32_t layout_padding_ = 0;
    std::int32_t layout_width_   = 0;
    bool render_pending_         = false;
    bool placement_pending_      = false;
    bool suppress_render_        = false;
    bool updating_input_         = false;
    bool visible_                = true;
    lv_timer_t *key_remap_timer_ = nullptr;
    static FilesView *key_remap_view_;
    static lv_indev_t *key_remap_indev_;
    static bool key_remap_active_;
};

#endif
