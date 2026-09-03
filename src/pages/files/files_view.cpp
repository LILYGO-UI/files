#include "pages/files/files_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr std::int32_t kMaximumFrameWidth     = 720;
constexpr std::int32_t kRegularPadding        = 24;
constexpr std::int32_t kCompactPadding        = 12;
constexpr std::int32_t kHomeIndicatorSpace    = 40;
constexpr std::int32_t kKeyboardMaximumHeight = 300;
constexpr const char *kSearchSymbol           = "\xEF\x80\x82";

FilesView *view_from_event(lv_event_t *event)
{
    return static_cast<FilesView *>(lv_event_get_user_data(event));
}

}  // namespace

FilesView::FilesView(FilesViewModel &view_model) noexcept : view_model_(view_model)
{
}

FilesView::~FilesView() noexcept
{
    destroy();
}

lv_obj_t *FilesView::create(lv_obj_t *parent)
{
    if (!parent) return nullptr;
    if (surface_) destroy();

    surface_ = components::create_page(parent);
    if (!surface_) return nullptr;
    lv_obj_set_name_static(surface_, "files_surface");
    lv_obj_set_style_pad_bottom(surface_, kHomeIndicatorSpace, 0);
    lv_obj_add_event_cb(surface_, size_changed, LV_EVENT_SIZE_CHANGED, this);

    frame_ = components::create_page(surface_);
    lv_obj_set_name_static(frame_, "files_frame");

    suppress_render_ = true;
    lv_subject_add_observer_obj(view_model_.content_revision_subject(), content_changed, surface_, this);
    suppress_render_ = false;

    render();
    lv_obj_update_layout(surface_);
    apply_layout();
    return surface_;
}

void FilesView::destroy() noexcept
{
    if (render_pending_) {
        lv_async_call_cancel(render_async, this);
        render_pending_ = false;
    }
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
    frame_   = nullptr;
    clear_page_references();
}

void FilesView::content_changed(lv_observer_t *observer, lv_subject_t *subject)
{
    (void)subject;
    auto *view = static_cast<FilesView *>(lv_observer_get_user_data(observer));
    if (view && !view->suppress_render_) view->schedule_render();
}

void FilesView::render_async(void *user_data)
{
    auto *view = static_cast<FilesView *>(user_data);
    if (!view) return;
    view->render_pending_ = false;
    if (view->surface_ && view->frame_) view->render();
}

void FilesView::size_changed(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->apply_layout();
}

void FilesView::back_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.back();
}

void FilesView::more_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.show(FilesScreen::More);
}

void FilesView::search_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.show(FilesScreen::Search);
}

void FilesView::row_clicked(lv_event_t *event)
{
    auto *binding = static_cast<RowBinding *>(lv_event_get_user_data(event));
    if (!binding || !binding->view) return;
    if (binding->search_result) {
        binding->view->view_model_.open_search_entry(binding->index);
    } else {
        binding->view->view_model_.open_entry(binding->index);
    }
}

void FilesView::query_changed(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view || view->updating_input_) return;
    auto *textarea         = lv_event_get_target_obj(event);
    view->suppress_render_ = true;
    view->view_model_.set_query(lv_textarea_get_text(textarea));
    view->suppress_render_ = false;
    if (view->view_model_.query() != lv_textarea_get_text(textarea)) {
        view->updating_input_ = true;
        lv_textarea_set_text(textarea, view->view_model_.query().c_str());
        view->updating_input_ = false;
    }
    if (view->search_error_) {
        lv_label_set_text(view->search_error_, "");
        lv_obj_add_flag(view->search_error_, LV_OBJ_FLAG_HIDDEN);
    }
    view->refresh_search_results();
}

void FilesView::search_input_activated(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view || !view->keyboard_) return;
    lv_obj_clear_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(view->keyboard_, lv_event_get_target_obj(event));
}

void FilesView::keyboard_done(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view || !view->keyboard_) return;
    lv_keyboard_set_textarea(view->keyboard_, nullptr);
    lv_obj_add_flag(view->keyboard_, LV_OBJ_FLAG_HIDDEN);
}

void FilesView::new_folder_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.show(FilesScreen::NewFolder);
}

void FilesView::folder_name_changed(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view || view->updating_input_) return;
    auto *textarea         = lv_event_get_target_obj(event);
    view->suppress_render_ = true;
    view->view_model_.set_folder_name(lv_textarea_get_text(textarea));
    view->suppress_render_ = false;
    if (view->view_model_.folder_name() != lv_textarea_get_text(textarea)) {
        view->updating_input_ = true;
        lv_textarea_set_text(textarea, view->view_model_.folder_name().c_str());
        view->updating_input_ = false;
    }
    if (view->folder_hint_) {
        lv_label_set_text(view->folder_hint_, "Folder will be created in the current folder.");
        lv_obj_set_style_text_color(view->folder_hint_, lv_color_hex(components::color_muted_text), 0);
    }
}

void FilesView::create_folder_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.create_folder();
}

void FilesView::make_plain(lv_obj_t *object) noexcept
{
    if (!object) return;
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t *FilesView::create_label(lv_obj_t *parent, const char *text, std::uint32_t size, std::uint32_t color)
{
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    (void)components::set_text_font(label, size);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void FilesView::schedule_render() noexcept
{
    if (render_pending_ || !surface_) return;
    render_pending_ = lv_async_call(render_async, this) == LV_RESULT_OK;
}

void FilesView::render()
{
    if (!frame_) return;
    if (page_) lv_obj_delete(page_);
    clear_page_references();

    page_ = components::create_page(frame_);
    lv_obj_set_name_static(page_, "files_page");
    base_ = create_base();

    switch (view_model_.screen()) {
        case FilesScreen::Home:
            build_home();
            break;
        case FilesScreen::Folder:
            build_folder();
            break;
        case FilesScreen::Search:
            build_search();
            break;
        case FilesScreen::Preview:
            build_preview();
            break;
        case FilesScreen::More:
            build_more();
            break;
        case FilesScreen::NewFolder:
            build_new_folder();
            break;
        case FilesScreen::Select:
        case FilesScreen::Move:
            create_underlay();
            break;
    }
    apply_layout();
}

void FilesView::clear_page_references() noexcept
{
    page_           = nullptr;
    base_           = nullptr;
    scroller_       = nullptr;
    overlay_        = nullptr;
    search_input_   = nullptr;
    search_count_   = nullptr;
    search_results_ = nullptr;
    search_error_   = nullptr;
    folder_input_   = nullptr;
    folder_hint_    = nullptr;
    keyboard_       = nullptr;
    padded_.fill(nullptr);
    padded_count_      = 0;
    row_binding_count_ = 0;
}

void FilesView::apply_layout() noexcept
{
    if (!surface_ || !frame_) return;
    std::int32_t width = lv_obj_get_content_width(surface_);
    if (width <= 0 && lv_obj_get_parent(surface_)) {
        width = lv_obj_get_content_width(lv_obj_get_parent(surface_));
    }
    if (width <= 0) return;

    const std::int32_t frame_width = std::min(width, kMaximumFrameWidth);
    const std::int32_t padding     = frame_width < 400 ? kCompactPadding : kRegularPadding;
    lv_obj_set_width(frame_, frame_width);
    lv_obj_align(frame_, LV_ALIGN_TOP_MID, 0, 0);

    for (std::size_t index = 0; index < padded_count_; ++index) {
        if (padded_[index]) {
            lv_obj_set_style_pad_hor(padded_[index], padding, 0);
        }
    }

    if (overlay_) {
        const std::int32_t overlay_width = std::max<std::int32_t>(280, std::min<std::int32_t>(472, frame_width - 32));
        if (view_model_.screen() == FilesScreen::NewFolder) {
            lv_obj_set_width(overlay_, overlay_width);
        }
    }
    if (keyboard_) {
        const std::int32_t available_height = lv_obj_get_content_height(surface_);
        const bool folder_dialog            = view_model_.screen() == FilesScreen::NewFolder;
        const std::int32_t minimum_height   = folder_dialog ? 160 : 180;
        const std::int32_t height_percent   = folder_dialog ? 36 : 42;
        const std::int32_t maximum_height   = folder_dialog ? 260 : kKeyboardMaximumHeight;
        const std::int32_t keyboard_height =
            std::min(maximum_height, std::max<std::int32_t>(minimum_height, available_height * height_percent / 100));
        lv_obj_set_height(keyboard_, keyboard_height);
        if (folder_dialog && overlay_) {
            lv_obj_align(overlay_, LV_ALIGN_TOP_MID, 0, 8);
        }
    }
}

void FilesView::register_padded(lv_obj_t *object) noexcept
{
    if (padded_count_ < padded_.size()) padded_[padded_count_++] = object;
}

lv_obj_t *FilesView::create_base()
{
    auto *base = components::create_page(page_);
    lv_obj_set_name_static(base, "files_content");
    lv_obj_set_flex_flow(base, LV_FLEX_FLOW_COLUMN);
    return base;
}

lv_obj_t *FilesView::create_scroller()
{
    auto *scroll = lv_obj_create(base_);
    lv_obj_set_name_static(scroll, "files_scroller");
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_radius(scroll, 0, 0);
    lv_obj_set_style_pad_hor(scroll, kRegularPadding, 0);
    lv_obj_set_style_pad_top(scroll, 8, 0);
    lv_obj_set_style_pad_bottom(scroll, 24, 0);
    lv_obj_set_style_pad_row(scroll, 14, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    register_padded(scroll);
    scroller_ = scroll;
    return scroll;
}

lv_obj_t *FilesView::create_header(lv_obj_t *parent)
{
    auto *header = lv_obj_create(parent);
    make_plain(header);
    lv_obj_set_name_static(header, "files_home_header");
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 58);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *title = create_label(header, "Files", 36, components::color_container_title);
    lv_obj_set_name_static(title, "files_title");
    lv_obj_set_flex_grow(title, 1);
    create_more_button(header);
    return header;
}

lv_obj_t *FilesView::create_navbar(lv_obj_t *parent, const char *back_text, const char *title, bool show_more)
{
    auto *bar = lv_obj_create(parent);
    make_plain(bar);
    lv_obj_set_name_static(bar, "files_navbar");
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 58);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *back = components::create_button(bar, components::color_page_background, 0);
    lv_obj_set_name_static(back, "files_back_button");
    lv_obj_set_size(back, 104, 48);
    lv_obj_set_style_pad_all(back, 0, 0);
    auto *back_label = create_label(back, "", 22, components::color_navigation_accent);
    lv_label_set_text_fmt(back_label, LV_SYMBOL_LEFT " %s", back_text ? back_text : "");
    lv_obj_set_width(back_label, LV_PCT(100));
    lv_label_set_long_mode(back_label, LV_LABEL_LONG_DOT);
    lv_obj_align(back_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, this);

    auto *heading = create_label(bar, title, 22, components::color_container_title);
    lv_obj_set_name_static(heading, "files_nav_title");
    lv_obj_set_width(heading, 0);
    lv_obj_set_height(heading, 30);
    lv_obj_set_flex_grow(heading, 1);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);

    if (show_more) {
        create_more_button(bar);
    } else {
        auto *spacer = lv_obj_create(bar);
        make_plain(spacer);
        lv_obj_set_size(spacer, 48, 48);
    }
    return bar;
}

lv_obj_t *FilesView::create_more_button(lv_obj_t *parent)
{
    auto *button = components::create_button(parent, components::color_secondary_control, LV_RADIUS_CIRCLE);
    lv_obj_set_name_static(button, "files_more_button");
    lv_obj_set_size(button, 48, 48);
    auto *icon = create_label(button, LV_SYMBOL_SETTINGS, 22, components::color_navigation_accent);
    lv_obj_center(icon);
    lv_obj_add_event_cb(button, more_clicked, LV_EVENT_CLICKED, this);
    return button;
}

lv_obj_t *FilesView::create_search_field(lv_obj_t *parent, const char *text, bool editable)
{
    auto *field = components::create_card(parent, 8);
    lv_obj_set_name_static(field, "files_search_field");
    lv_obj_set_width(field, LV_PCT(100));
    lv_obj_set_height(field, 52);
    lv_obj_set_style_pad_hor(field, 14, 0);
    lv_obj_set_style_pad_ver(field, 0, 0);
    lv_obj_set_style_pad_column(field, 10, 0);
    lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);

    auto *search_icon = create_label(field, kSearchSymbol, 22, components::color_placeholder_text);
    lv_obj_set_size(search_icon, 28, 28);

    if (editable) {
        auto *textarea = lv_textarea_create(field);
        make_plain(textarea);
        lv_obj_set_name_static(textarea, "files_search_input");
        lv_obj_set_width(textarea, 0);
        lv_obj_set_flex_grow(textarea, 1);
        lv_textarea_set_one_line(textarea, true);
        lv_obj_set_height(textarea, 48);
        lv_textarea_set_max_length(textarea, FilesViewModel::text_capacity - 1U);
        lv_textarea_set_text(textarea, text ? text : "");
        lv_textarea_set_placeholder_text(textarea, "Search files and folders");
        (void)components::set_text_font(textarea, 22);
        lv_obj_set_style_text_color(textarea, lv_color_hex(components::color_text), 0);
        lv_obj_add_event_cb(textarea, query_changed, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(textarea, search_input_activated, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(textarea, search_input_activated, LV_EVENT_CLICKED, this);
        search_input_ = textarea;
    } else {
        auto *placeholder = create_label(field, text, 22, components::color_placeholder_text);
        lv_obj_set_name_static(placeholder, "files_search_placeholder");
        lv_obj_set_width(placeholder, 0);
        lv_obj_set_flex_grow(placeholder, 1);
        lv_label_set_long_mode(placeholder, LV_LABEL_LONG_DOT);
        lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(field, search_clicked, LV_EVENT_CLICKED, this);
    }
    return field;
}

lv_obj_t *FilesView::create_file_icon(lv_obj_t *parent, FilesEntryKind kind)
{
    const bool directory =
        kind == FilesEntryKind::Folder || kind == FilesEntryKind::Download || kind == FilesEntryKind::Music;
    const char *symbol  = LV_SYMBOL_FILE;
    std::uint32_t color = components::color_navigation_accent;
    if (directory) symbol = LV_SYMBOL_DIRECTORY;
    if (kind == FilesEntryKind::Download) {
        symbol = LV_SYMBOL_DOWNLOAD;
        color  = components::color_download_accent;
    } else if (kind == FilesEntryKind::Music) {
        symbol = LV_SYMBOL_AUDIO;
    } else if (kind == FilesEntryKind::Pdf) {
        symbol = "PDF";
        color  = components::color_destructive;
    } else if (kind == FilesEntryKind::Text) {
        symbol = "TXT";
        color  = components::color_muted_text;
    } else if (kind == FilesEntryKind::Binary) {
        symbol = "BIN";
    }

    auto *box = lv_obj_create(parent);
    make_plain(box);
    lv_obj_set_size(box, 48, 48);
    auto *icon = create_label(box, symbol, directory ? 28U : 14U, color);
    lv_obj_center(icon);
    return box;
}

lv_obj_t *FilesView::create_entry_list(lv_obj_t *parent, bool search_results, std::int32_t row_height, bool populate)
{
    auto *list = components::create_card(parent, 8);
    lv_obj_set_name_static(list, search_results ? "files_search_results" : "files_entry_list");
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    const std::size_t count = populate ? (search_results ? view_model_.search_count() : view_model_.entry_count()) : 0U;
    for (std::size_t index = 0; index < count; ++index) {
        const FilesEntryPresentation *entry =
            search_results ? view_model_.search_entry(index) : view_model_.entry(index);
        if (!entry) continue;
        if (lv_obj_get_child_count(list) > 0) create_divider(list);
        create_entry_row(list, *entry, index, search_results, row_height);
    }
    return list;
}

lv_obj_t *FilesView::create_entry_row(lv_obj_t *parent, const FilesEntryPresentation &entry, std::size_t index,
                                      bool search_result, std::int32_t row_height)
{
    auto *row = components::create_button(parent, components::color_container_background, 0);
    lv_obj_set_name_static(row, "files_entry_row");
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, row_height);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    create_file_icon(row, entry.kind);

    auto *name = create_label(row, entry.name.c_str(), 22, components::color_text);
    lv_obj_set_name_static(name, "files_entry_name");
    lv_obj_set_width(name, 0);
    lv_obj_set_height(name, 30);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    if (!entry.detail.empty()) {
        auto *detail = create_label(row, entry.detail.c_str(), 14, components::color_muted_text);
        lv_obj_set_style_max_width(detail, 112, 0);
        lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    }
    auto *chevron = create_label(row, LV_SYMBOL_RIGHT, 22, components::color_muted_text);
    lv_obj_set_width(chevron, 20);

    if (row_binding_count_ < row_bindings_.size()) {
        auto &binding = row_bindings_[row_binding_count_++];
        binding       = {this, index, search_result};
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, &binding);
    }
    return row;
}

void FilesView::create_divider(lv_obj_t *parent)
{
    auto *divider = lv_obj_create(parent);
    make_plain(divider);
    lv_obj_set_width(divider, LV_PCT(100));
    lv_obj_set_height(divider, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
}

void FilesView::create_storage_card(lv_obj_t *parent)
{
    auto *card = components::create_card(parent, 8);
    lv_obj_set_name_static(card, "files_storage_card");
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto *header = lv_obj_create(card);
    make_plain(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 30);
    lv_obj_set_style_pad_column(header, 8, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *title = create_label(header, "Home Storage", 22, components::color_container_title);
    lv_obj_set_name_static(title, "files_storage_title");
    lv_obj_set_flex_grow(title, 1);
    auto *summary = create_label(header, view_model_.storage().summary.c_str(), 14, components::color_muted_text);
    lv_obj_set_name_static(summary, "files_storage_summary");

    auto *bar = lv_bar_create(card);
    lv_obj_set_name_static(bar, "files_storage_bar");
    lv_obj_set_size(bar, LV_PCT(100), 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, view_model_.storage().used_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(components::color_secondary_control), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(components::color_navigation_accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
}

void FilesView::create_underlay()
{
    if (view_model_.content_screen() == FilesScreen::Home) {
        build_home();
    } else {
        build_folder();
    }
}

void FilesView::create_scrim()
{
    auto *scrim = lv_obj_create(page_);
    lv_obj_set_name_static(scrim, "files_overlay_scrim");
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_hex(components::color_scrim), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_30, 0);
    lv_obj_set_style_border_width(scrim, 0, 0);
    lv_obj_set_style_radius(scrim, 0, 0);
    lv_obj_set_style_pad_all(scrim, 0, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scrim, back_clicked, LV_EVENT_CLICKED, this);
    lv_obj_align(scrim, LV_ALIGN_CENTER, 0, 0);
}

void FilesView::build_home()
{
    auto *scroll = create_scroller();
    create_header(scroll);
    create_search_field(scroll, "Search files and folders", false);
    create_storage_card(scroll);

    auto *section = lv_obj_create(scroll);
    make_plain(section);
    lv_obj_set_name_static(section, "files_home_section");
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(section, 8, 0);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    auto *title = create_label(section, "My Files", 22, components::color_container_title);
    lv_obj_set_name_static(title, "files_section_title");

    if (!view_model_.error_text().empty()) {
        auto *error = create_label(section, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(error, "files_error_label");
    } else if (view_model_.entry_count() == 0) {
        auto *empty = create_label(section, "This folder is empty", 22, components::color_muted_text);
        lv_obj_set_name_static(empty, "files_empty_state");
    } else {
        create_entry_list(section, false, 76);
    }
}

void FilesView::build_folder()
{
    auto *scroll = create_scroller();
    create_navbar(scroll, "Files", view_model_.directory_name().c_str(), true);
    create_search_field(scroll, "Search this folder", false);

    char count[48]{};
    const std::size_t entry_count = view_model_.entry_count();
    std::snprintf(count, sizeof(count), "%zu %s", entry_count, entry_count == 1U ? "item" : "items");
    auto *count_label = create_label(scroll, count, 14, components::color_muted_text);
    lv_obj_set_name_static(count_label, "files_entry_count");
    auto *path = create_label(scroll, view_model_.display_path().c_str(), 14, components::color_muted_text);
    lv_obj_set_name_static(path, "files_path_label");
    lv_obj_set_width(path, LV_PCT(100));
    lv_obj_set_height(path, 24);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);

    if (!view_model_.error_text().empty()) {
        auto *error = create_label(scroll, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(error, "files_error_label");
    } else if (view_model_.entry_count() == 0) {
        auto *empty = create_label(scroll, "This folder is empty", 22, components::color_muted_text);
        lv_obj_set_name_static(empty, "files_empty_state");
    } else {
        create_entry_list(scroll, false, 86);
    }
}

void FilesView::build_search()
{
    auto *scroll = create_scroller();
    create_navbar(scroll, view_model_.directory_name().c_str(), "Search", false);
    create_search_field(scroll, view_model_.query().c_str(), true);

    auto *toolbar = lv_obj_create(scroll);
    make_plain(toolbar);
    lv_obj_set_name_static(toolbar, "files_search_toolbar");
    lv_obj_set_width(toolbar, LV_PCT(100));
    lv_obj_set_height(toolbar, 34);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    search_count_ = create_label(toolbar, "", 14, components::color_muted_text);
    lv_obj_set_name_static(search_count_, "files_result_count");
    auto *path = create_label(toolbar, view_model_.display_path().c_str(), 14, components::color_muted_text);
    lv_obj_set_name_static(path, "files_path_label");
    lv_obj_set_width(path, LV_PCT(58));
    lv_obj_set_height(path, 24);
    lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(path, LV_TEXT_ALIGN_RIGHT, 0);

    if (!view_model_.error_text().empty()) {
        search_error_ = create_label(scroll, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(search_error_, "files_search_error");
        lv_obj_set_width(search_error_, LV_PCT(100));
        lv_label_set_long_mode(search_error_, LV_LABEL_LONG_WRAP);
    }

    search_results_ = create_entry_list(scroll, true, 86, false);
    keyboard_       = lv_keyboard_create(base_);
    lv_obj_set_name_static(keyboard_, "files_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_height(keyboard_, 260);
    lv_obj_set_style_max_height(keyboard_, kKeyboardMaximumHeight, 0);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(components::color_control_background), 0);
    lv_obj_set_style_border_width(keyboard_, 0, 0);
    lv_obj_set_style_radius(keyboard_, 0, 0);
    (void)components::set_text_font(keyboard_, 22, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard_, search_input_);
    lv_obj_add_event_cb(keyboard_, keyboard_done, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_done, LV_EVENT_CANCEL, this);
    lv_obj_add_state(search_input_, LV_STATE_FOCUSED);
    refresh_search_results();
}

void FilesView::build_preview()
{
    const FilesEntryPresentation *entry = view_model_.preview_entry();
    const char *name                    = entry ? entry->name.c_str() : "File";
    const char *detail                  = entry ? entry->detail.c_str() : "";
    const char *tag_text                = entry && entry->kind == FilesEntryKind::Text ? "TXT" : "FILE";

    auto *scroll = create_scroller();
    create_navbar(scroll, view_model_.directory_name().c_str(), name, false);
    auto *metadata = create_label(scroll, detail, 14, components::color_muted_text);
    lv_obj_set_name_static(metadata, "files_preview_metadata");
    lv_obj_set_width(metadata, LV_PCT(100));
    lv_obj_set_style_text_align(metadata, LV_TEXT_ALIGN_CENTER, 0);

    auto *document = components::create_card(scroll, 8);
    lv_obj_set_name_static(document, "files_preview_document");
    lv_obj_set_width(document, LV_PCT(100));
    lv_obj_set_height(document, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(document, 28, 0);
    lv_obj_set_style_pad_row(document, 18, 0);
    lv_obj_set_flex_flow(document, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(document, LV_OBJ_FLAG_SCROLLABLE);

    auto *tag = lv_obj_create(document);
    lv_obj_set_size(tag, 56, 34);
    lv_obj_set_style_bg_color(tag, lv_color_hex(components::color_selected_background), 0);
    lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tag, 0, 0);
    lv_obj_set_style_radius(tag, 8, 0);
    lv_obj_set_style_pad_all(tag, 0, 0);
    auto *tag_label = create_label(tag, tag_text, 14, components::color_navigation_accent);
    lv_obj_center(tag_label);

    auto *heading = create_label(document, name, 28, components::color_container_title);
    lv_obj_set_name_static(heading, "files_preview_title");
    lv_obj_set_width(heading, LV_PCT(100));
    lv_label_set_long_mode(heading, LV_LABEL_LONG_WRAP);
    create_divider(document);
    auto *body = create_label(document, view_model_.preview().c_str(), 22, components::color_text);
    lv_obj_set_name_static(body, "files_preview_body");
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_height(body, LV_SIZE_CONTENT);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body, 8, 0);
}

void FilesView::build_more()
{
    create_underlay();
    create_scrim();

    auto *sheet = components::create_card(page_, 8);
    overlay_    = sheet;
    lv_obj_set_name_static(sheet, "files_more_sheet");
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(sheet, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(sheet, 20, 0);
    lv_obj_set_style_pad_row(sheet, 12, 0);
    lv_obj_set_flex_flow(sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);

    auto *title = create_label(sheet, "More Actions", 22, components::color_container_title);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    auto *new_folder = components::create_button(sheet, components::color_container_background, 0);
    lv_obj_set_name_static(new_folder, "files_new_folder_button");
    lv_obj_set_size(new_folder, LV_PCT(100), 58);
    lv_obj_set_style_pad_hor(new_folder, 16, 0);
    lv_obj_set_style_pad_column(new_folder, 14, 0);
    lv_obj_set_flex_flow(new_folder, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(new_folder, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    create_label(new_folder, LV_SYMBOL_PLUS, 22, components::color_navigation_accent);
    auto *new_folder_text = create_label(new_folder, "New Folder", 22, components::color_text);
    lv_obj_set_flex_grow(new_folder_text, 1);
    create_label(new_folder, LV_SYMBOL_RIGHT, 22, components::color_muted_text);
    lv_obj_add_event_cb(new_folder, new_folder_clicked, LV_EVENT_CLICKED, this);

    auto *cancel = components::create_button(sheet, components::color_secondary_control, 8);
    lv_obj_set_name_static(cancel, "files_more_cancel");
    lv_obj_set_size(cancel, LV_PCT(100), 54);
    auto *cancel_text = create_label(cancel, "Cancel", 22, components::color_navigation_accent);
    lv_obj_center(cancel_text);
    lv_obj_add_event_cb(cancel, back_clicked, LV_EVENT_CLICKED, this);
}

void FilesView::build_new_folder()
{
    create_underlay();
    create_scrim();

    auto *dialog = components::create_card(page_, 8);
    overlay_     = dialog;
    lv_obj_set_name_static(dialog, "files_new_folder_dialog");
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(dialog, 472, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_set_style_pad_row(dialog, 16, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, -32);

    auto *title = create_label(dialog, "New Folder", 28, components::color_container_title);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    const bool has_error = !view_model_.error_text().empty();
    folder_hint_         = create_label(
        dialog, has_error ? view_model_.error_text().c_str() : "Folder will be created in the current folder.", 14,
        has_error ? components::color_destructive : components::color_muted_text);
    lv_obj_set_name_static(folder_hint_, "files_folder_hint");
    lv_obj_set_width(folder_hint_, LV_PCT(100));
    lv_label_set_long_mode(folder_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(folder_hint_, LV_TEXT_ALIGN_CENTER, 0);

    auto *input = lv_textarea_create(dialog);
    lv_obj_set_name_static(input, "files_folder_name_input");
    lv_obj_set_width(input, LV_PCT(100));
    lv_textarea_set_one_line(input, true);
    lv_obj_set_height(input, 56);
    lv_textarea_set_max_length(input, FilesViewModel::text_capacity - 1U);
    lv_textarea_set_text(input, view_model_.folder_name().c_str());
    (void)components::set_text_font(input, 22);
    lv_obj_set_style_text_color(input, lv_color_hex(components::color_text), 0);
    lv_obj_set_style_bg_color(input, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(components::color_navigation_accent), 0);
    lv_obj_set_style_border_width(input, 2, 0);
    lv_obj_set_style_radius(input, 8, 0);
    lv_obj_set_style_pad_hor(input, 14, 0);
    lv_obj_add_event_cb(input, folder_name_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(input, search_input_activated, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(input, search_input_activated, LV_EVENT_CLICKED, this);
    folder_input_ = input;

    auto *actions = lv_obj_create(dialog);
    make_plain(actions);
    lv_obj_set_size(actions, LV_PCT(100), 54);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);

    auto *cancel = components::create_button(actions, components::color_secondary_control, 8);
    lv_obj_set_name_static(cancel, "files_folder_cancel");
    lv_obj_set_height(cancel, 54);
    lv_obj_set_flex_grow(cancel, 1);
    auto *cancel_text = create_label(cancel, "Cancel", 22, components::color_navigation_accent);
    lv_obj_center(cancel_text);
    lv_obj_add_event_cb(cancel, back_clicked, LV_EVENT_CLICKED, this);

    auto *create = components::create_button(actions, components::color_action, 8);
    lv_obj_set_name_static(create, "files_folder_create");
    lv_obj_set_height(create, 54);
    lv_obj_set_flex_grow(create, 1);
    auto *create_text = create_label(create, "Create", 22, components::color_on_action);
    lv_obj_center(create_text);
    lv_obj_add_event_cb(create, create_folder_clicked, LV_EVENT_CLICKED, this);

    keyboard_ = lv_keyboard_create(page_);
    lv_obj_set_name_static(keyboard_, "files_folder_keyboard");
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_set_height(keyboard_, 220);
    lv_obj_set_style_max_height(keyboard_, 260, 0);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(components::color_control_background), 0);
    lv_obj_set_style_border_width(keyboard_, 0, 0);
    lv_obj_set_style_radius(keyboard_, 0, 0);
    (void)components::set_text_font(keyboard_, 22, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard_, folder_input_);
    lv_obj_add_event_cb(keyboard_, keyboard_done, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, keyboard_done, LV_EVENT_CANCEL, this);
    lv_obj_add_state(folder_input_, LV_STATE_FOCUSED);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void FilesView::refresh_search_results()
{
    if (!search_count_ || !search_results_) return;
    char count[48]{};
    const std::size_t result_count = view_model_.search_count();
    std::snprintf(count, sizeof(count), "%zu %s", result_count, result_count == 1U ? "result" : "results");
    lv_label_set_text(search_count_, count);

    row_binding_count_ = 0;
    lv_obj_clean(search_results_);
    if (result_count == 0) {
        auto *empty = create_label(search_results_, "No matching items", 22, components::color_muted_text);
        lv_obj_set_name_static(empty, "files_search_empty");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_all(empty, 20, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (std::size_t index = 0; index < result_count; ++index) {
        const auto *entry = view_model_.search_entry(index);
        if (!entry) continue;
        if (lv_obj_get_child_count(search_results_) > 0) {
            create_divider(search_results_);
        }
        create_entry_row(search_results_, *entry, index, true, 86);
    }
}
