#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_VIEW_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_VIEW_HPP

#include "pages/files/files_view_model.hpp"

#include <lvgl.h>

#include <array>
#include <cstddef>

class FilesView {
public:
    explicit FilesView(FilesViewModel &view_model) noexcept;
    ~FilesView() noexcept;

    FilesView(const FilesView &)            = delete;
    FilesView &operator=(const FilesView &) = delete;
    FilesView(FilesView &&)                 = delete;
    FilesView &operator=(FilesView &&)      = delete;

    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;

private:
    struct RowBinding {
        FilesView *view    = nullptr;
        std::size_t index  = 0;
        bool search_result = false;
    };

    static void content_changed(lv_observer_t *observer, lv_subject_t *subject);
    static void render_async(void *user_data);
    static void size_changed(lv_event_t *event);
    static void back_clicked(lv_event_t *event);
    static void more_clicked(lv_event_t *event);
    static void search_clicked(lv_event_t *event);
    static void row_clicked(lv_event_t *event);
    static void query_changed(lv_event_t *event);
    static void search_input_activated(lv_event_t *event);
    static void keyboard_done(lv_event_t *event);
    static void new_folder_clicked(lv_event_t *event);
    static void folder_name_changed(lv_event_t *event);
    static void create_folder_clicked(lv_event_t *event);

    static void make_plain(lv_obj_t *object) noexcept;
    static lv_obj_t *create_label(lv_obj_t *parent, const char *text, std::uint32_t size, std::uint32_t color);

    void schedule_render() noexcept;
    void render();
    void clear_page_references() noexcept;
    void apply_layout() noexcept;
    void register_padded(lv_obj_t *object) noexcept;

    lv_obj_t *create_base();
    lv_obj_t *create_scroller();
    lv_obj_t *create_header(lv_obj_t *parent);
    lv_obj_t *create_navbar(lv_obj_t *parent, const char *back_text, const char *title, bool show_more);
    lv_obj_t *create_more_button(lv_obj_t *parent);
    lv_obj_t *create_search_field(lv_obj_t *parent, const char *text, bool editable);
    lv_obj_t *create_file_icon(lv_obj_t *parent, FilesEntryKind kind);
    lv_obj_t *create_entry_list(lv_obj_t *parent, bool search_results, std::int32_t row_height, bool populate = true);
    lv_obj_t *create_entry_row(lv_obj_t *parent, const FilesEntryPresentation &entry, std::size_t index,
                               bool search_result, std::int32_t row_height);
    void create_divider(lv_obj_t *parent);
    void create_storage_card(lv_obj_t *parent);
    void create_underlay();
    void create_scrim();

    void build_home();
    void build_folder();
    void build_search();
    void build_preview();
    void build_more();
    void build_new_folder();
    void refresh_search_results();

    FilesViewModel &view_model_;
    lv_obj_t *surface_        = nullptr;
    lv_obj_t *frame_          = nullptr;
    lv_obj_t *page_           = nullptr;
    lv_obj_t *base_           = nullptr;
    lv_obj_t *scroller_       = nullptr;
    lv_obj_t *overlay_        = nullptr;
    lv_obj_t *search_input_   = nullptr;
    lv_obj_t *search_count_   = nullptr;
    lv_obj_t *search_results_ = nullptr;
    lv_obj_t *search_error_   = nullptr;
    lv_obj_t *folder_input_   = nullptr;
    lv_obj_t *folder_hint_    = nullptr;
    lv_obj_t *keyboard_       = nullptr;
    std::array<lv_obj_t *, 12> padded_{};
    std::size_t padded_count_ = 0;
    std::array<RowBinding, FilesViewModel::maximum_entries> row_bindings_{};
    std::size_t row_binding_count_ = 0;
    bool render_pending_           = false;
    bool suppress_render_          = false;
    bool updating_input_           = false;
};

#endif
