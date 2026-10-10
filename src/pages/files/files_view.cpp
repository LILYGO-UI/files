#include "pages/files/files_view.hpp"

#include "components/components.hpp"
#include "pages/files/files_icons.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace {

constexpr std::int32_t kMaximumFrameWidth     = 1280;
constexpr std::int32_t kRegularPadding        = 24;
constexpr std::int32_t kCompactPadding        = 12;
constexpr std::int32_t kHomeIndicatorSpace    = 40;
constexpr std::int32_t kKeyboardMaximumHeight = 300;
constexpr std::int32_t kMinimumPinnedContent  = 120;
constexpr std::int32_t kSectionSpacing        = 16;
constexpr std::int32_t kNavbarHeight          = 72;
constexpr std::int32_t kNavButtonSize         = 56;
constexpr std::int32_t kCapsulePadding        = 22;
constexpr std::int32_t kSearchFieldHeight     = 52;
constexpr std::int32_t kGridMinimumCellWidth  = 160;
constexpr std::int32_t kGridMinimumColumns    = 2;
constexpr std::int32_t kGridMaximumColumns    = 8;
constexpr std::int32_t kGridColumnGap         = 16;
constexpr std::int32_t kGridRowGap            = 20;
constexpr std::int32_t kCellRadius            = 16;
constexpr std::int32_t kListRowHeight         = 80;
constexpr std::int32_t kListLeadingWidth      = 28;
constexpr std::int32_t kSelectIndicatorSize   = 28;
constexpr std::int32_t kPreviewMaximumWidth   = 860;
constexpr std::int32_t kMenuWidth             = 340;
constexpr std::int32_t kMenuRadius            = 28;
constexpr std::int32_t kMenuItemHeight        = 56;
constexpr std::int32_t kMenuTop               = 8;
constexpr std::int32_t kDialogMaximumWidth    = 472;
constexpr std::int32_t kDialogRadius          = 28;
constexpr std::int32_t kDialogButtonHeight    = 54;
constexpr std::int32_t kActionMenuGap         = 12;
constexpr std::int32_t kActionItemHeight      = 48;
constexpr std::int32_t kActionTileRadius      = 16;
constexpr std::int32_t kToolbarTopPadding     = 12;
constexpr std::int32_t kToolbarBottomPadding  = 8;
constexpr std::int32_t kToolbarButtonWidth    = 64;
constexpr std::int32_t kInfoIconHeight        = 160;
constexpr std::int32_t kInfoRowMinimumHeight  = 56;
constexpr std::int32_t kInfoCardRadius        = 16;
constexpr const char *kSearchSymbol           = "\xEF\x80\x82";  // U+F002 magnifying glass
constexpr const char *kMoreSymbol             = "\xEF\x85\x81";  // U+F141 horizontal ellipsis
constexpr const char *kSelectSymbol           = "\xEF\x81\x98";  // U+F058 check circle
constexpr const char *kNewFolderSymbol        = "\xEF\x99\x9E";  // U+F65E folder plus
constexpr const char *kIconsSymbol            = "\xEF\x80\x89";  // U+F009 large grid
constexpr const char *kListSymbol             = "\xEF\x83\x8A";  // U+F0CA bulleted list
constexpr const char *kPasteSymbol            = "\xEF\x83\xAA";  // U+F0EA paste
constexpr const char *kCopySymbol             = "\xEF\x83\x85";  // U+F0C5 copy
constexpr const char *kMoveSymbol             = "\xEF\x81\xBB";  // U+F07B folder
constexpr const char *kInfoSymbol             = "\xEF\x81\x9A";  // U+F05A info circle
constexpr const char *kRenameSymbol           = "\xEF\x8C\x83";  // U+F303 pencil
constexpr const char *kCompressSymbol         = "\xEF\x86\x87";  // U+F187 archive
constexpr const char *kDuplicateSymbol        = "\xEF\x89\x8D";  // U+F24D clone
constexpr const char *kDeleteSymbol           = "\xEF\x8B\xAD";  // U+F2ED trash

// Commands of the long-press menu and of the selection toolbar and its menu, kept in their menu bindings.
enum class ItemAction : std::int32_t {
    Copy,
    Move,
    Info,
    Rename,
    Compress,
    Duplicate,
    NewFolder,
    Delete,
};

FilesView *view_from_event(lv_event_t *event)
{
    return static_cast<FilesView *>(lv_event_get_user_data(event));
}

bool shows_directory_content(FilesScreen screen) noexcept
{
    return screen == FilesScreen::Home || screen == FilesScreen::Folder || screen == FilesScreen::Select ||
           screen == FilesScreen::More || screen == FilesScreen::NewFolder || screen == FilesScreen::Actions ||
           screen == FilesScreen::Rename || screen == FilesScreen::ConfirmDelete ||
           screen == FilesScreen::SelectionActions;
}

// Menus, dialogs and selection mode show their own errors, so the directory beneath them stays as it was.
bool reports_own_error(FilesScreen screen) noexcept
{
    return screen == FilesScreen::More || screen == FilesScreen::NewFolder || screen == FilesScreen::Actions ||
           screen == FilesScreen::Rename || screen == FilesScreen::ConfirmDelete || screen == FilesScreen::Select ||
           screen == FilesScreen::SelectionActions;
}

bool is_name_dialog(FilesScreen screen) noexcept
{
    return screen == FilesScreen::NewFolder || screen == FilesScreen::Rename;
}

const char *name_dialog_hint(FilesScreen screen) noexcept
{
    return screen == FilesScreen::Rename ? "Enter a new name for this item."
                                         : "Folder will be created in the current folder.";
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
    if (placement_pending_) {
        lv_async_call_cancel(place_async, this);
        placement_pending_ = false;
    }
    if (surface_) lv_obj_delete(surface_);
    surface_ = nullptr;
    frame_   = nullptr;
    clear_page_references();
    render_key_.clear();
    scroll_key_.clear();
    scroll_y_ = 0;
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

void FilesView::place_async(void *user_data)
{
    auto *view = static_cast<FilesView *>(user_data);
    if (!view) return;
    view->placement_pending_ = false;
    if (!view->surface_) return;
    lv_obj_update_layout(view->surface_);
    view->place_action_menu(true);
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
    FilesView *view = binding->view;
    if (binding->source == RowSource::Search) {
        view->view_model_.open_search_entry(binding->index);
        return;
    }
    if (binding->source == RowSource::MoveDestination) {
        view->view_model_.open_move_destination(binding->index);
        return;
    }

    // Selection changes are applied in place so the content keeps its scroll position.
    const bool selecting   = view->view_model_.screen() == FilesScreen::Select;
    view->suppress_render_ = selecting;
    view->view_model_.activate_entry(binding->index);
    view->suppress_render_ = false;
    if (selecting) view->refresh_selection();
}

void FilesView::row_long_pressed(lv_event_t *event)
{
    auto *binding = static_cast<RowBinding *>(lv_event_get_user_data(event));
    if (!binding || !binding->view || binding->source != RowSource::Directory) return;
    FilesView *view          = binding->view;
    const FilesScreen screen = view->view_model_.screen();
    if (screen != FilesScreen::Home && screen != FilesScreen::Folder) return;
    // The release that ends the long press must not also open the item.
    if (lv_indev_t *indev = lv_indev_active()) lv_indev_wait_release(indev);
    view->view_model_.show_actions(binding->index);
}

void FilesView::action_clicked(lv_event_t *event)
{
    auto *binding = static_cast<MenuBinding *>(lv_event_get_user_data(event));
    if (!binding || !binding->view) return;
    FilesViewModel &view_model = binding->view->view_model_;
    switch (static_cast<ItemAction>(binding->value)) {
        case ItemAction::Copy:
            view_model.copy_action_items();
            break;
        case ItemAction::Move:
            view_model.show(FilesScreen::Move);
            break;
        case ItemAction::Info:
            view_model.show(FilesScreen::Info);
            break;
        case ItemAction::Rename:
            view_model.show(FilesScreen::Rename);
            break;
        case ItemAction::Compress:
            view_model.compress_action_items();
            break;
        case ItemAction::Duplicate:
            view_model.duplicate_action_items();
            break;
        case ItemAction::NewFolder:
            view_model.new_folder_with_action_items();
            break;
        case ItemAction::Delete:
            view_model.show(FilesScreen::ConfirmDelete);
            break;
    }
}

void FilesView::paste_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.paste();
}

void FilesView::info_open_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.open_action_entry();
}

void FilesView::delete_confirm_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.remove_action_items();
}

void FilesView::move_up_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.move_destination_up();
}

void FilesView::move_confirm_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.move_action_items();
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

void FilesView::select_mode_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.show(FilesScreen::Select);
}

void FilesView::select_all_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view) return;
    view->suppress_render_ = true;
    view->view_model_.toggle_select_all();
    view->suppress_render_ = false;
    view->refresh_selection();
}

void FilesView::selection_more_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.show(FilesScreen::SelectionActions);
}

void FilesView::view_mode_clicked(lv_event_t *event)
{
    auto *binding = static_cast<MenuBinding *>(lv_event_get_user_data(event));
    if (binding && binding->view) binding->view->view_model_.set_view_mode(static_cast<FilesViewMode>(binding->value));
}

void FilesView::sort_clicked(lv_event_t *event)
{
    auto *binding = static_cast<MenuBinding *>(lv_event_get_user_data(event));
    if (binding && binding->view) binding->view->view_model_.set_sort(static_cast<FilesSortKey>(binding->value));
}

void FilesView::new_folder_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->view_model_.show(FilesScreen::NewFolder);
}

void FilesView::name_changed(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view || view->updating_input_) return;
    auto *textarea           = lv_event_get_target_obj(event);
    const FilesScreen screen = view->view_model_.screen();
    const bool renaming      = screen == FilesScreen::Rename;
    view->suppress_render_   = true;
    if (renaming) {
        view->view_model_.set_rename_name(lv_textarea_get_text(textarea));
    } else {
        view->view_model_.set_folder_name(lv_textarea_get_text(textarea));
    }
    view->suppress_render_     = false;
    const std::string &bounded = renaming ? view->view_model_.rename_name() : view->view_model_.folder_name();
    if (bounded != lv_textarea_get_text(textarea)) {
        view->updating_input_ = true;
        lv_textarea_set_text(textarea, bounded.c_str());
        view->updating_input_ = false;
    }
    if (view->name_hint_) {
        lv_label_set_text(view->name_hint_, name_dialog_hint(screen));
        lv_obj_set_style_text_color(view->name_hint_, lv_color_hex(components::color_muted_text), 0);
    }
}

void FilesView::name_confirm_clicked(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view) return;
    if (view->view_model_.screen() == FilesScreen::Rename) {
        view->view_model_.rename_action_entry();
    } else {
        view->view_model_.create_folder();
    }
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

lv_obj_t *FilesView::create_box(lv_obj_t *parent)
{
    // Layout-only container. It must not be clickable, or taps on it would not reach the button around it.
    auto *box = lv_obj_create(parent);
    make_plain(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

lv_obj_t *FilesView::create_label(lv_obj_t *parent, const char *text, std::uint32_t size, std::uint32_t color)
{
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    (void)components::set_text_font(label, size);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void FilesView::limit_lines(lv_obj_t *label, std::uint32_t size, std::int32_t lines) noexcept
{
    // Content-sized labels wrap freely; a maximum height turns the overflow into trailing dots.
    const lv_font_t *font = components::font(size);
    if (!label || !font || lines <= 0) return;
    const std::int32_t line_space = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    const std::int32_t padding =
        lv_obj_get_style_pad_top(label, LV_PART_MAIN) + lv_obj_get_style_pad_bottom(label, LV_PART_MAIN);
    lv_obj_set_style_max_height(label, lv_font_get_line_height(font) * lines + line_space * (lines - 1) + padding, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

void FilesView::apply_selection_state(lv_obj_t *item, bool selected) noexcept
{
    // The checked state drives the item highlight and the filled ring; the checkmark shows only when selected.
    if (!item) return;
    lv_obj_t *ring = lv_obj_find_by_name(item, "files_select_indicator");
    for (lv_obj_t *object : {item, ring}) {
        if (!object) continue;
        if (selected) {
            lv_obj_add_state(object, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(object, LV_STATE_CHECKED);
        }
    }
    lv_obj_t *check = ring ? lv_obj_get_child(ring, 0) : nullptr;
    if (check) {
        if (selected) {
            lv_obj_clear_flag(check, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void FilesView::schedule_render() noexcept
{
    if (render_pending_ || !surface_) return;
    render_pending_ = lv_async_call(render_async, this) == LV_RESULT_OK;
}

void FilesView::schedule_placement() noexcept
{
    if (placement_pending_ || !surface_) return;
    placement_pending_ = lv_async_call(place_async, this) == LV_RESULT_OK;
}

std::string FilesView::content_key() const
{
    if (!shows_directory_content(view_model_.screen())) return {};
    return std::to_string(static_cast<int>(view_model_.view_mode())) + ':' + view_model_.display_path();
}

void FilesView::render()
{
    if (!frame_) return;
    // The same directory content returns where it was scrolled to, for example when a menu or selection
    // mode opens over it, or after a page such as Move or a preview closes.
    const std::string key = content_key();
    if (scroller_ && !render_key_.empty()) {
        scroll_key_ = render_key_;
        scroll_y_   = lv_obj_get_scroll_y(scroller_);
    }
    const std::int32_t previous_scroll = !key.empty() && key == scroll_key_ ? scroll_y_ : 0;
    if (page_) lv_obj_delete(page_);
    clear_page_references();
    render_key_ = key;

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
        case FilesScreen::Select:
            build_select();
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
        case FilesScreen::Actions:
            build_actions();
            break;
        case FilesScreen::Info:
            build_info();
            break;
        case FilesScreen::Move:
            build_move();
            break;
        case FilesScreen::NewFolder:
        case FilesScreen::Rename:
            build_name_dialog();
            break;
        case FilesScreen::ConfirmDelete:
            build_delete();
            break;
        case FilesScreen::SelectionActions:
            build_selection_actions();
            break;
    }
    // Lay out once so the chrome placement below measures the new chrome.
    lv_obj_update_layout(surface_);
    apply_layout();

    if (previous_scroll > 0 && scroller_) {
        lv_obj_update_layout(surface_);
        const std::int32_t maximum = std::max<std::int32_t>(0, lv_obj_get_scroll_bottom(scroller_));
        lv_obj_scroll_to_y(scroller_, std::min(previous_scroll, maximum), LV_ANIM_OFF);
    }
    if (view_model_.screen() == FilesScreen::Actions) {
        lv_obj_update_layout(surface_);
        place_action_menu(true);
    }
}

void FilesView::clear_page_references() noexcept
{
    page_             = nullptr;
    base_             = nullptr;
    body_             = nullptr;
    chrome_           = nullptr;
    scroller_         = nullptr;
    entry_grid_       = nullptr;
    overlay_          = nullptr;
    action_preview_   = nullptr;
    nav_title_        = nullptr;
    select_all_label_ = nullptr;
    selection_error_  = nullptr;
    toolbar_          = nullptr;
    search_input_     = nullptr;
    search_count_     = nullptr;
    search_results_   = nullptr;
    search_error_     = nullptr;
    name_input_       = nullptr;
    name_hint_        = nullptr;
    keyboard_         = nullptr;
    padded_.fill(nullptr);
    padded_count_       = 0;
    row_binding_count_  = 0;
    menu_binding_count_ = 0;
}

void FilesView::apply_layout() noexcept
{
    if (!surface_ || !frame_) return;
    std::int32_t width = lv_obj_get_content_width(surface_);
    if (width <= 0 && lv_obj_get_parent(surface_)) {
        width = lv_obj_get_content_width(lv_obj_get_parent(surface_));
    }
    if (width <= 0) return;

    const std::int32_t height          = lv_obj_get_content_height(surface_);
    const std::int32_t frame_width     = std::min(width, kMaximumFrameWidth);
    const std::int32_t padding         = frame_width < 400 ? kCompactPadding : kRegularPadding;
    const std::int32_t available_width = std::max<std::int32_t>(0, frame_width - 2 * padding);
    lv_obj_set_width(frame_, frame_width);
    lv_obj_align(frame_, LV_ALIGN_TOP_MID, 0, 0);
    layout_padding_ = padding;
    layout_width_   = available_width;

    for (std::size_t index = 0; index < padded_count_; ++index) {
        if (padded_[index]) {
            lv_obj_set_style_pad_hor(padded_[index], padding, 0);
        }
    }
    apply_grid_layout(available_width);

    std::int32_t docked_keyboard_height = 0;
    if (overlay_) {
        if (view_model_.screen() == FilesScreen::More) {
            // An iOS pull-down menu that opens over the more button and scrolls when it is taller than the page.
            lv_obj_set_width(overlay_, std::min(available_width, kMenuWidth));
            lv_obj_set_style_max_height(overlay_, std::max<std::int32_t>(0, height - kMenuTop - padding), 0);
            lv_obj_align(overlay_, LV_ALIGN_TOP_RIGHT, -padding, kMenuTop);
        } else if (view_model_.screen() == FilesScreen::Actions) {
            // The item moves when the grid reflows; place the menu again once the new layout is final.
            place_action_menu(false);
            schedule_placement();
        } else if (view_model_.screen() == FilesScreen::SelectionActions) {
            // The selection menu opens above the toolbar's more button, sharing its right edge.
            const std::int32_t above = kToolbarBottomPadding + kNavButtonSize + kActionMenuGap;
            lv_obj_set_width(overlay_, std::min(available_width, kMenuWidth));
            lv_obj_set_style_max_height(overlay_, std::max<std::int32_t>(0, height - kMenuTop - above), 0);
            lv_obj_align(overlay_, LV_ALIGN_BOTTOM_RIGHT, -padding, -above);
        } else {
            lv_obj_set_width(overlay_, std::min(available_width, kDialogMaximumWidth));
        }
    }
    if (keyboard_) {
        const bool folder_dialog          = is_name_dialog(view_model_.screen());
        const std::int32_t minimum_height = folder_dialog ? 160 : 180;
        const std::int32_t height_percent = folder_dialog ? 36 : 42;
        const std::int32_t maximum_height = folder_dialog ? 260 : kKeyboardMaximumHeight;
        const std::int32_t keyboard_height =
            std::min(maximum_height, std::max<std::int32_t>(minimum_height, height * height_percent / 100));
        lv_obj_set_height(keyboard_, keyboard_height);
        if (folder_dialog && overlay_) {
            lv_obj_align(overlay_, LV_ALIGN_TOP_MID, 0, 8);
        }
        if (lv_obj_get_parent(keyboard_) == base_) docked_keyboard_height = keyboard_height;
    }
    apply_chrome_placement(padding, height - docked_keyboard_height);
}

void FilesView::apply_grid_layout(std::int32_t available_width) noexcept
{
    // Columns follow the container width: three on a portrait phone, more as the width grows.
    if (available_width <= 0) return;
    const std::int32_t columns =
        std::clamp((available_width + kGridColumnGap) / (kGridMinimumCellWidth + kGridColumnGap), kGridMinimumColumns,
                   kGridMaximumColumns);
    cell_width_ = std::max<std::int32_t>(1, (available_width - kGridColumnGap * (columns - 1)) / columns);
    if (view_model_.view_mode() != FilesViewMode::Icons) return;

    for (lv_obj_t *grid : {entry_grid_, search_results_}) {
        if (!grid) continue;
        const std::uint32_t count = lv_obj_get_child_count(grid);
        for (std::uint32_t index = 0; index < count; ++index) {
            lv_obj_t *child = lv_obj_get_child(grid, static_cast<std::int32_t>(index));
            if (lv_obj_check_type(child, &lv_button_class)) lv_obj_set_width(child, cell_width_);
        }
    }
}

void FilesView::apply_chrome_placement(std::int32_t padding, std::int32_t body_height) noexcept
{
    if (!body_ || !chrome_ || !scroller_) return;

    // The chrome is pinned above the content like an iOS navigation bar. In very short containers,
    // such as a landscape phone with the keyboard open, it scrolls with the content instead so the
    // content keeps a usable height. The same object moves, preserving focus, input and scrolling.
    const std::int32_t toolbar_height = toolbar_ ? lv_obj_get_height(toolbar_) : 0;
    const bool pinned = body_height - lv_obj_get_height(chrome_) - toolbar_height >= kMinimumPinnedContent;
    lv_obj_t *parent  = pinned ? body_ : scroller_;
    if (lv_obj_get_parent(chrome_) != parent) {
        lv_obj_set_parent(chrome_, parent);
        lv_obj_move_to_index(chrome_, 0);
    }
    lv_obj_set_style_pad_hor(chrome_, pinned ? padding : 0, 0);
}

void FilesView::place_action_menu(bool reveal_item) noexcept
{
    if (!page_ || !overlay_ || view_model_.screen() != FilesScreen::Actions) return;
    lv_obj_t *item = find_entry_item(view_model_.action_index());
    if (item && reveal_item) {
        // The long-pressed item must be fully visible for the menu to open next to it.
        lv_obj_scroll_to_view_recursive(item, LV_ANIM_OFF);
        lv_obj_update_layout(surface_);
    }
    const std::int32_t padding     = layout_padding_;
    const std::int32_t page_width  = lv_obj_get_width(page_);
    const std::int32_t page_height = lv_obj_get_height(page_);
    const std::int32_t menu_width  = std::min(layout_width_, kMenuWidth);
    const std::int32_t max_height  = std::max<std::int32_t>(0, page_height - 2 * padding);
    lv_obj_set_width(overlay_, menu_width);
    lv_obj_set_style_max_height(overlay_, max_height, 0);
    const std::int32_t menu_height = std::min(lv_obj_get_height(overlay_), max_height);

    // The lifted preview covers the item exactly; without the item the menu opens in the middle.
    lv_area_t page_area{};
    lv_obj_get_coords(page_, &page_area);
    std::int32_t left   = page_width / 2;
    std::int32_t right  = left;
    std::int32_t top    = (page_height - menu_height) / 2;
    std::int32_t bottom = top;
    if (item) {
        lv_area_t area{};
        lv_obj_get_coords(item, &area);
        left   = area.x1 - page_area.x1;
        right  = area.x2 - page_area.x1 + 1;
        top    = area.y1 - page_area.y1;
        bottom = area.y2 - page_area.y1 + 1;
        if (action_preview_) {
            lv_obj_set_width(action_preview_, right - left);
            lv_obj_set_pos(action_preview_, left, top);
        }
    }

    // Below the item when it fits, otherwise above it. Short pages, such as a landscape phone, put the
    // menu beside the item instead.
    const std::int32_t lowest = std::max(padding, page_height - padding - menu_height);
    std::int32_t x =
        std::clamp((left + right - menu_width) / 2, padding, std::max(padding, page_width - padding - menu_width));
    std::int32_t y = lowest;
    if (bottom + kActionMenuGap + menu_height <= page_height - padding) {
        y = bottom + kActionMenuGap;
    } else if (top - kActionMenuGap - menu_height >= padding) {
        y = top - kActionMenuGap - menu_height;
    } else {
        y = std::clamp(top, padding, lowest);
        if (right + kActionMenuGap + menu_width <= page_width - padding) {
            x = right + kActionMenuGap;
        } else if (left - kActionMenuGap - menu_width >= padding) {
            x = left - kActionMenuGap - menu_width;
        }
    }
    lv_obj_set_pos(overlay_, x, y);
}

lv_obj_t *FilesView::find_entry_item(std::size_t index) const noexcept
{
    if (!entry_grid_) return nullptr;
    const std::uint32_t count = lv_obj_get_child_count(entry_grid_);
    for (std::uint32_t child = 0; child < count; ++child) {
        lv_obj_t *item      = lv_obj_get_child(entry_grid_, static_cast<std::int32_t>(child));
        const auto *binding = static_cast<const RowBinding *>(lv_obj_get_user_data(item));
        if (binding && binding->index == index) return item;
    }
    return nullptr;
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

    body_ = lv_obj_create(base);
    make_plain(body_);
    lv_obj_set_name_static(body_, "files_body");
    lv_obj_set_width(body_, LV_PCT(100));
    lv_obj_set_flex_grow(body_, 1);
    lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    return base;
}

lv_obj_t *FilesView::create_scroller()
{
    // Navigation chrome followed by the scrolling content; see apply_chrome_placement().
    chrome_ = lv_obj_create(body_);
    make_plain(chrome_);
    lv_obj_set_name_static(chrome_, "files_chrome");
    lv_obj_set_size(chrome_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_bottom(chrome_, 8, 0);
    lv_obj_set_style_pad_row(chrome_, 10, 0);
    lv_obj_set_flex_flow(chrome_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(chrome_, LV_OBJ_FLAG_SCROLLABLE);

    auto *scroll = lv_obj_create(body_);
    lv_obj_set_name_static(scroll, "files_scroller");
    lv_obj_set_width(scroll, LV_PCT(100));
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_radius(scroll, 0, 0);
    lv_obj_set_style_pad_hor(scroll, kRegularPadding, 0);
    lv_obj_set_style_pad_top(scroll, 4, 0);
    lv_obj_set_style_pad_bottom(scroll, 24, 0);
    lv_obj_set_style_pad_row(scroll, kSectionSpacing, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    register_padded(scroll);
    scroller_ = scroll;
    return scroll;
}

lv_obj_t *FilesView::create_navbar_row()
{
    auto *bar = lv_obj_create(chrome_);
    make_plain(bar);
    lv_obj_set_name_static(bar, "files_navbar");
    lv_obj_set_size(bar, LV_PCT(100), kNavbarHeight);
    lv_obj_set_style_pad_column(bar, 12, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    // The circular buttons cast a soft shadow below the bar's own bounds.
    lv_obj_add_flag(bar, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    return bar;
}

lv_obj_t *FilesView::create_nav_titles(lv_obj_t *bar, const char *title, const char *subtitle,
                                       const char *subtitle_name)
{
    auto *titles = create_box(bar);
    lv_obj_set_name_static(titles, "files_nav_titles");
    lv_obj_set_size(titles, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(titles, 1);
    lv_obj_set_style_pad_row(titles, 2, 0);
    lv_obj_set_flex_flow(titles, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(titles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *heading = create_label(titles, title, 28, components::color_container_title);
    lv_obj_set_name_static(heading, "files_nav_title");
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    limit_lines(heading, 28, 1);

    if (subtitle && subtitle_name) {
        auto *caption = create_label(titles, subtitle, 14, components::color_muted_text);
        lv_obj_set_name_static(caption, subtitle_name);
        lv_obj_set_width(caption, LV_PCT(100));
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        limit_lines(caption, 14, 1);
    }
    return heading;
}

lv_obj_t *FilesView::create_navbar(const char *title, const char *subtitle, const char *subtitle_name, bool show_back,
                                   bool show_more)
{
    auto *bar = create_navbar_row();

    // Equal-width placeholders keep the title centered when a side has no button.
    const auto create_spacer = [bar]() { lv_obj_set_size(create_box(bar), kNavButtonSize, kNavButtonSize); };
    if (show_back) {
        create_circle_button(bar, LV_SYMBOL_LEFT, "files_back_button", back_clicked);
    } else {
        create_spacer();
    }
    create_nav_titles(bar, title, subtitle, subtitle_name);
    if (show_more) {
        create_circle_button(bar, kMoreSymbol, "files_more_button", more_clicked);
    } else {
        create_spacer();
    }
    return bar;
}

lv_obj_t *FilesView::create_circle_button(lv_obj_t *parent, const char *symbol, const char *name,
                                          lv_event_cb_t callback)
{
    auto *button = components::create_button(parent, components::color_container_background, LV_RADIUS_CIRCLE);
    lv_obj_set_name_static(button, name);
    lv_obj_set_size(button, kNavButtonSize, kNavButtonSize);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 24, 0);
    lv_obj_set_style_shadow_offset_y(button, 4, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(components::color_shadow), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_10, 0);
    auto *icon = create_label(button, symbol, 22, components::color_container_title);
    lv_obj_center(icon);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    return button;
}

lv_obj_t *FilesView::create_capsule_button(lv_obj_t *parent, const char *text, const char *name, lv_event_cb_t callback)
{
    auto *button = create_circle_button(parent, text, name, callback);
    lv_obj_set_width(button, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(button, kCapsulePadding, 0);
    return button;
}

lv_obj_t *FilesView::create_search_field(lv_obj_t *parent, const char *text, bool editable)
{
    auto *field = components::create_card(parent, LV_RADIUS_CIRCLE);
    lv_obj_set_name_static(field, "files_search_field");
    lv_obj_set_size(field, LV_PCT(100), kSearchFieldHeight);
    lv_obj_set_style_border_width(field, 0, 0);
    lv_obj_set_style_pad_hor(field, 18, 0);
    lv_obj_set_style_pad_ver(field, 0, 0);
    lv_obj_set_style_pad_column(field, 12, 0);
    lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);

    create_label(field, kSearchSymbol, 22, components::color_placeholder_text);

    if (editable) {
        auto *textarea = lv_textarea_create(field);
        make_plain(textarea);
        lv_obj_set_name_static(textarea, "files_search_input");
        lv_obj_set_width(textarea, 0);
        lv_obj_set_flex_grow(textarea, 1);
        lv_textarea_set_one_line(textarea, true);
        if (const lv_font_t *font = components::font(22)) {
            lv_obj_set_style_pad_ver(textarea, std::max<std::int32_t>(0, (48 - lv_font_get_line_height(font)) / 2), 0);
        }
        lv_textarea_set_max_length(textarea, FilesViewModel::text_capacity - 1U);
        lv_textarea_set_text(textarea, text ? text : "");
        lv_textarea_set_placeholder_text(textarea, "Search");
        (void)components::set_text_font(textarea, 22);
        lv_obj_set_style_text_color(textarea, lv_color_hex(components::color_text), 0);
        lv_obj_set_style_text_color(textarea, lv_color_hex(components::color_placeholder_text),
                                    LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
        lv_obj_set_style_border_width(textarea, 2, LV_PART_CURSOR);
        lv_obj_set_style_border_color(textarea, lv_color_hex(components::color_navigation_accent), LV_PART_CURSOR);
        lv_obj_add_event_cb(textarea, query_changed, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(textarea, search_input_activated, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(textarea, search_input_activated, LV_EVENT_CLICKED, this);
        search_input_ = textarea;
    } else {
        auto *placeholder = create_label(field, text, 22, components::color_placeholder_text);
        lv_obj_set_name_static(placeholder, "files_search_placeholder");
        lv_obj_set_width(placeholder, 0);
        lv_obj_set_flex_grow(placeholder, 1);
        limit_lines(placeholder, 22, 1);
        lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(field, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
        lv_obj_add_event_cb(field, search_clicked, LV_EVENT_CLICKED, this);
    }
    return field;
}

lv_obj_t *FilesView::create_entry_collection(lv_obj_t *parent, bool search_results, bool populate)
{
    // Entries are shown as an icon grid or as a list, following the view mode chosen in the more menu.
    auto *collection = create_box(parent);
    lv_obj_set_name_static(collection, search_results ? "files_search_results" : "files_entry_list");
    lv_obj_set_size(collection, LV_PCT(100), LV_SIZE_CONTENT);
    if (view_model_.view_mode() == FilesViewMode::List) {
        lv_obj_set_flex_flow(collection, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_style_pad_column(collection, kGridColumnGap, 0);
        lv_obj_set_style_pad_row(collection, kGridRowGap, 0);
        lv_obj_set_flex_flow(collection, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(collection, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }

    const std::size_t count = populate ? (search_results ? view_model_.search_count() : view_model_.entry_count()) : 0U;
    const RowSource source  = search_results ? RowSource::Search : RowSource::Directory;
    for (std::size_t index = 0; index < count; ++index) {
        const FilesEntryPresentation *entry =
            search_results ? view_model_.search_entry(index) : view_model_.entry(index);
        if (entry) create_entry_item(collection, *entry, index, source);
    }
    return collection;
}

lv_obj_t *FilesView::create_entry_item(lv_obj_t *parent, const FilesEntryPresentation &entry, std::size_t index,
                                       RowSource source)
{
    // Move destinations are always listed as rows.
    const bool selecting = source == RowSource::Directory && view_model_.selecting();
    const bool as_row    = view_model_.view_mode() == FilesViewMode::List || source == RowSource::MoveDestination;
    lv_obj_t *item = as_row ? create_list_row(parent, entry, selecting) : create_grid_cell(parent, entry, selecting);
    if (selecting) apply_selection_state(item, view_model_.is_selected(index));

    if (row_binding_count_ < row_bindings_.size()) {
        auto &binding = row_bindings_[row_binding_count_++];
        binding       = {this, index, source};
        lv_obj_set_user_data(item, &binding);
        lv_obj_add_event_cb(item, row_clicked, LV_EVENT_CLICKED, &binding);
        // A long press on an item of the open folder shows its context menu.
        if (source == RowSource::Directory) {
            lv_obj_add_event_cb(item, row_long_pressed, LV_EVENT_LONG_PRESSED, &binding);
        }
    }
    return item;
}

lv_obj_t *FilesView::create_grid_cell(lv_obj_t *parent, const FilesEntryPresentation &entry, bool selecting)
{
    // One icon-view cell: icon, a name of at most two lines, then the date and size or the item count.
    auto *cell = lv_button_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_name_static(cell, "files_entry_row");
    lv_obj_set_size(cell, cell_width_ > 0 ? cell_width_ : kGridMinimumCellWidth, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(cell, kCellRadius, 0);
    lv_obj_set_style_bg_color(cell, lv_color_hex(components::color_selected_background), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(cell, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(cell, 4, 0);
    lv_obj_set_style_pad_ver(cell, 8, 0);
    lv_obj_set_style_pad_row(cell, 2, 0);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *icon = files_icons::create(cell, entry.kind, entry.type_label);
    if (selecting) {
        // The selection ring overlaps the shape's lower-right corner, as in iOS Files. It stays inside the
        // icon box because LVGL skips children that are drawn outside their parent's area.
        auto *ring                 = create_selection_indicator(icon);
        const std::int32_t box     = files_icons::box_width(files_icons::grid_height);
        const std::int32_t shape   = files_icons::shape_width(entry.kind, files_icons::grid_height);
        const std::int32_t x_shift = std::min(shape / 2 - 4, (box - kSelectIndicatorSize) / 2);
        lv_obj_align(ring, LV_ALIGN_BOTTOM_MID, x_shift, 0);
    }

    auto *name = create_label(cell, entry.name.c_str(), 22, components::color_text);
    lv_obj_set_name_static(name, "files_entry_name");
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_pad_top(name, 10, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    limit_lines(name, 22, 2);

    const auto create_detail = [cell](const std::string &text, const char *object_name) {
        if (text.empty()) return;
        auto *detail = create_label(cell, text.c_str(), 14, components::color_muted_text);
        lv_obj_set_name_static(detail, object_name);
        lv_obj_set_width(detail, LV_PCT(100));
        lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
        limit_lines(detail, 14, 1);
    };
    // Folders show only their item count, files the date and size.
    if (!entry.is_directory) create_detail(entry.modified, "files_entry_date");
    create_detail(entry.detail, "files_entry_detail");
    return cell;
}

lv_obj_t *FilesView::create_list_row(lv_obj_t *parent, const FilesEntryPresentation &entry, bool selecting)
{
    // One list row: a leading folder chevron or selection ring, a small icon, then the name over
    // "date - size" with an inset separator, as in the iOS Files list view.
    auto *row = lv_button_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_name_static(row, "files_entry_row");
    lv_obj_set_size(row, LV_PCT(100), kListRowHeight);
    lv_obj_set_style_radius(row, kCellRadius, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(components::color_selected_background), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(row, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (selecting) {
        create_selection_indicator(row);
    } else {
        auto *chevron =
            create_label(row, entry.is_directory ? LV_SYMBOL_RIGHT : "", 22, components::color_navigation_accent);
        lv_obj_set_width(chevron, kListLeadingWidth);
        lv_obj_set_style_text_align(chevron, LV_TEXT_ALIGN_CENTER, 0);
    }
    files_icons::create(row, entry.kind, entry.type_label, files_icons::list_height);

    auto *texts = create_box(row);
    lv_obj_set_size(texts, 0, LV_PCT(100));
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_style_pad_row(texts, 2, 0);
    lv_obj_set_style_border_side(texts, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(texts, 1, 0);
    lv_obj_set_style_border_color(texts, lv_color_hex(components::color_border), 0);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    auto *name = create_label(texts, entry.name.c_str(), 22, components::color_text);
    lv_obj_set_name_static(name, "files_entry_name");
    lv_obj_set_width(name, LV_PCT(100));
    limit_lines(name, 22, 1);
    auto *summary = create_label(texts, entry.summary.c_str(), 14, components::color_muted_text);
    lv_obj_set_name_static(summary, "files_entry_detail");
    lv_obj_set_width(summary, LV_PCT(100));
    limit_lines(summary, 14, 1);
    return row;
}

lv_obj_t *FilesView::create_selection_indicator(lv_obj_t *parent)
{
    auto *ring = create_box(parent);
    lv_obj_set_name_static(ring, "files_select_indicator");
    lv_obj_set_size(ring, kSelectIndicatorSize, kSelectIndicatorSize);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ring, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(components::color_selection_ring), 0);
    lv_obj_set_style_bg_color(ring, lv_color_hex(components::color_navigation_accent), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(ring, lv_color_hex(components::color_navigation_accent), LV_STATE_CHECKED);

    auto *check = create_label(ring, LV_SYMBOL_OK, 14, components::color_on_action);
    lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(check);
    return ring;
}

void FilesView::create_selection_toolbar()
{
    // The iOS selection toolbar below the content: Duplicate, Move and Delete in a floating capsule, and
    // the selection menu behind a circular more button. Share and tags are not offered.
    toolbar_ = create_box(body_);
    lv_obj_set_name_static(toolbar_, "files_select_toolbar");
    lv_obj_set_size(toolbar_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(toolbar_, kToolbarTopPadding, 0);
    lv_obj_set_style_pad_bottom(toolbar_, kToolbarBottomPadding, 0);
    lv_obj_set_flex_flow(toolbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // The buttons cast a soft shadow beyond the toolbar's bounds.
    lv_obj_add_flag(toolbar_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    register_padded(toolbar_);

    auto *actions = create_box(toolbar_);
    lv_obj_set_name_static(actions, "files_select_actions");
    lv_obj_set_size(actions, LV_SIZE_CONTENT, kNavButtonSize);
    lv_obj_set_style_radius(actions, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(actions, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(actions, 24, 0);
    lv_obj_set_style_shadow_offset_y(actions, 4, 0);
    lv_obj_set_style_shadow_color(actions, lv_color_hex(components::color_shadow), 0);
    lv_obj_set_style_shadow_opa(actions, LV_OPA_10, 0);
    lv_obj_set_style_pad_hor(actions, 4, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(actions, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    create_toolbar_button(actions, kDuplicateSymbol, "files_select_duplicate",
                          static_cast<std::int32_t>(ItemAction::Duplicate));
    create_toolbar_button(actions, kMoveSymbol, "files_select_move", static_cast<std::int32_t>(ItemAction::Move));
    create_toolbar_button(actions, kDeleteSymbol, "files_select_delete", static_cast<std::int32_t>(ItemAction::Delete));

    create_circle_button(toolbar_, kMoreSymbol, "files_select_more", selection_more_clicked);
    apply_toolbar_state();
}

lv_obj_t *FilesView::create_toolbar_button(lv_obj_t *parent, const char *symbol, const char *name, std::int32_t action)
{
    auto *button = components::create_button(parent, components::color_container_background, LV_RADIUS_CIRCLE);
    lv_obj_set_name_static(button, name);
    lv_obj_set_size(button, kToolbarButtonWidth, kNavButtonSize);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    auto *icon = create_label(button, symbol, 22, components::color_container_title);
    lv_obj_center(icon);
    lv_obj_add_event_cb(button, action_clicked, LV_EVENT_CLICKED, bind_menu(action));
    return button;
}

void FilesView::apply_toolbar_state() noexcept
{
    // The selection commands need at least one selected item; until then they are disabled and muted.
    if (!toolbar_) return;
    const bool enabled                     = view_model_.selected_count() > 0U;
    const std::uint32_t color              = enabled ? components::color_container_title : components::color_muted_text;
    static constexpr const char *buttons[] = {"files_select_duplicate", "files_select_move", "files_select_delete",
                                              "files_select_more"};
    for (const char *name : buttons) {
        lv_obj_t *button = lv_obj_find_by_name(toolbar_, name);
        if (!button) continue;
        if (enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(color), 0);
    }
}

lv_obj_t *FilesView::create_menu_card(const char *name)
{
    auto *menu = components::create_card(page_, kMenuRadius);
    overlay_   = menu;
    lv_obj_set_name_static(menu, name);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(menu, kMenuWidth, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(menu, 0, 0);
    lv_obj_set_style_pad_hor(menu, 0, 0);
    lv_obj_set_style_pad_ver(menu, 8, 0);
    lv_obj_set_style_pad_row(menu, 0, 0);
    lv_obj_set_style_shadow_width(menu, 48, 0);
    lv_obj_set_style_shadow_offset_y(menu, 12, 0);
    lv_obj_set_style_shadow_color(menu, lv_color_hex(components::color_shadow), 0);
    lv_obj_set_style_shadow_opa(menu, LV_OPA_20, 0);
    lv_obj_set_style_clip_corner(menu, true, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(menu, LV_DIR_VER);
    return menu;
}

void FilesView::create_menu_error(lv_obj_t *menu)
{
    // A failed command keeps its menu open and explains why at the top.
    if (view_model_.error_text().empty()) return;
    auto *error = create_label(menu, view_model_.error_text().c_str(), 14, components::color_destructive);
    lv_obj_set_name_static(error, "files_menu_error");
    lv_obj_set_width(error, LV_PCT(100));
    lv_obj_set_style_pad_hor(error, 20, 0);
    lv_obj_set_style_pad_ver(error, 8, 0);
    lv_label_set_long_mode(error, LV_LABEL_LONG_WRAP);
}

lv_obj_t *FilesView::create_menu_item(lv_obj_t *menu, const char *text, const char *subtitle, const char *symbol,
                                      bool checked, const char *name, lv_event_cb_t callback, void *user_data)
{
    // Menu rows follow iOS: a checkmark column, an optional symbol, then the title and an optional subtitle.
    auto *item = components::create_button(menu, components::color_container_background, 0);
    lv_obj_set_name_static(item, name);
    lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(item, kMenuItemHeight, 0);
    lv_obj_set_style_pad_hor(item, 20, 0);
    lv_obj_set_style_pad_ver(item, 8, 0);
    lv_obj_set_style_pad_column(item, 12, 0);
    // Rows are transparent so the card's rounded corners stay visible; only the pressed row is filled.
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (checked) lv_obj_add_state(item, LV_STATE_CHECKED);

    auto *check = create_label(item, checked ? LV_SYMBOL_OK : "", 22, components::color_container_title);
    lv_obj_set_width(check, 28);
    if (symbol) {
        auto *icon = create_label(item, symbol, 22, components::color_container_title);
        lv_obj_set_width(icon, 32);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    }

    auto *texts = create_box(item);
    lv_obj_set_size(texts, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    auto *title = create_label(texts, text, 22, components::color_text);
    lv_obj_set_width(title, LV_PCT(100));
    limit_lines(title, 22, 1);
    if (subtitle) {
        auto *caption = create_label(texts, subtitle, 14, components::color_muted_text);
        lv_obj_set_name_static(caption, "files_menu_subtitle");
    }
    lv_obj_add_event_cb(item, callback, LV_EVENT_CLICKED, user_data);
    return item;
}

lv_obj_t *FilesView::create_action_item(lv_obj_t *menu, const char *text, const char *symbol, const char *name,
                                        std::int32_t action, bool destructive)
{
    // Context menu rows lead with their symbol; Delete uses the destructive color.
    const std::uint32_t symbol_color = destructive ? components::color_destructive : components::color_container_title;
    const std::uint32_t text_color   = destructive ? components::color_destructive : components::color_text;
    // Rows are a little tighter than the more menu so the whole menu fits a landscape phone.
    auto *item = components::create_button(menu, components::color_container_background, 0);
    lv_obj_set_name_static(item, name);
    lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(item, kActionItemHeight, 0);
    lv_obj_set_style_pad_hor(item, 20, 0);
    lv_obj_set_style_pad_ver(item, 6, 0);
    lv_obj_set_style_pad_column(item, 16, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *icon = create_label(item, symbol, 22, symbol_color);
    lv_obj_set_width(icon, 32);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    auto *title = create_label(item, text, 22, text_color);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    limit_lines(title, 22, 2);
    lv_obj_add_event_cb(item, action_clicked, LV_EVENT_CLICKED, bind_menu(action));
    return item;
}

lv_obj_t *FilesView::create_action_tile(lv_obj_t *row, const char *text, const char *symbol, const char *name,
                                        std::int32_t action)
{
    // The leading commands of the context menu sit side by side, a symbol above a short title.
    auto *tile = components::create_button(row, components::color_container_background, kActionTileRadius);
    lv_obj_set_name_static(tile, name);
    lv_obj_set_size(tile, 0, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tile, 1);
    lv_obj_set_style_pad_ver(tile, 10, 0);
    lv_obj_set_style_pad_row(tile, 4, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    create_label(tile, symbol, 22, components::color_container_title);
    create_label(tile, text, 14, components::color_container_title);
    lv_obj_add_event_cb(tile, action_clicked, LV_EVENT_CLICKED, bind_menu(action));
    return tile;
}

lv_obj_t *FilesView::create_dialog(const char *name)
{
    // iOS alert: a rounded card with a centered title and message above capsule buttons.
    auto *dialog = components::create_card(page_, kDialogRadius);
    overlay_     = dialog;
    lv_obj_set_name_static(dialog, name);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(dialog, kDialogMaximumWidth, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_set_style_pad_row(dialog, 16, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, -32);
    return dialog;
}

lv_obj_t *FilesView::create_dialog_button(lv_obj_t *parent, const char *text, const char *name,
                                          std::uint32_t background, std::uint32_t foreground, lv_event_cb_t callback)
{
    auto *button = components::create_button(parent, background, LV_RADIUS_CIRCLE);
    lv_obj_set_name_static(button, name);
    lv_obj_set_height(button, kDialogButtonHeight);
    lv_obj_set_flex_grow(button, 1);
    auto *label = create_label(button, text, 22, foreground);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    return button;
}

lv_obj_t *FilesView::create_keyboard(lv_obj_t *parent, const char *name, lv_obj_t *textarea, std::int32_t height)
{
    auto *keyboard = lv_keyboard_create(parent);
    lv_obj_set_name_static(keyboard, name);
    lv_obj_set_width(keyboard, LV_PCT(100));
    lv_obj_set_height(keyboard, height);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(components::color_control_background), 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_radius(keyboard, 0, 0);
    (void)components::set_text_font(keyboard, 22, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_CANCEL, this);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    return keyboard;
}

lv_obj_t *FilesView::create_info_row(lv_obj_t *list, const char *key, const std::string &value, const char *name)
{
    // One information row: the label on the left and the value right-aligned, wrapping when it is long.
    auto *row = create_box(list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, kInfoRowMinimumHeight, 0);
    lv_obj_set_style_pad_ver(row, 14, 0);
    lv_obj_set_style_pad_column(row, 16, 0);
    if (lv_obj_get_child_count(list) > 1U) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(components::color_border), 0);
    }
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_label(row, key, 22, components::color_muted_text);
    auto *value_label = create_label(row, value.c_str(), 22, components::color_text);
    lv_obj_set_name_static(value_label, name);
    lv_obj_set_width(value_label, 0);
    lv_obj_set_flex_grow(value_label, 1);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    return row;
}

void FilesView::create_menu_divider(lv_obj_t *menu)
{
    auto *gap = create_box(menu);
    lv_obj_set_size(gap, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(gap, 20, 0);
    lv_obj_set_style_pad_ver(gap, 6, 0);
    create_divider(gap);
}

FilesView::MenuBinding *FilesView::bind_menu(std::int32_t value) noexcept
{
    if (menu_binding_count_ >= menu_bindings_.size()) return nullptr;
    auto &binding = menu_bindings_[menu_binding_count_++];
    binding       = {this, value};
    return &binding;
}

void FilesView::create_divider(lv_obj_t *parent)
{
    auto *divider = create_box(parent);
    lv_obj_set_width(divider, LV_PCT(100));
    lv_obj_set_height(divider, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
}

void FilesView::create_directory_content(lv_obj_t *scroll)
{
    auto *search = create_search_field(scroll, "Search", false);
    if (view_model_.selecting()) {
        // Searching is unavailable while selecting; the field stays so the content does not shift.
        lv_obj_clear_flag(search, LV_OBJ_FLAG_CLICKABLE);
    }

    if (!view_model_.error_text().empty() && !reports_own_error(view_model_.screen())) {
        auto *error = create_label(scroll, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(error, "files_error_label");
        lv_obj_set_width(error, LV_PCT(100));
        lv_obj_set_style_pad_top(error, 24, 0);
        lv_obj_set_style_text_align(error, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(error, LV_LABEL_LONG_WRAP);
        return;
    }

    const std::size_t entry_count = view_model_.entry_count();
    if (entry_count == 0) {
        auto *empty = create_label(scroll, "This folder is empty", 22, components::color_muted_text);
        lv_obj_set_name_static(empty, "files_empty_state");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_top(empty, 48, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    entry_grid_ = create_entry_collection(scroll, false);
    char count[48]{};
    std::snprintf(count, sizeof(count), "%zu %s", entry_count, entry_count == 1U ? "item" : "items");
    auto *count_label = create_label(scroll, count, 14, components::color_muted_text);
    lv_obj_set_name_static(count_label, "files_entry_count");
    lv_obj_set_width(count_label, LV_PCT(100));
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_CENTER, 0);
}

void FilesView::create_underlay()
{
    // Menus and prompts that act on the selected items open over selection mode.
    if (view_model_.selecting()) {
        build_select();
    } else if (view_model_.content_screen() == FilesScreen::Home) {
        build_home();
    } else {
        build_folder();
    }
}

void FilesView::create_scrim(lv_opa_t opacity)
{
    auto *scrim = lv_obj_create(page_);
    lv_obj_set_name_static(scrim, "files_overlay_scrim");
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_hex(components::color_scrim), 0);
    lv_obj_set_style_bg_opa(scrim, opacity, 0);
    lv_obj_set_style_border_width(scrim, 0, 0);
    lv_obj_set_style_radius(scrim, 0, 0);
    lv_obj_set_style_pad_all(scrim, 0, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scrim, back_clicked, LV_EVENT_CLICKED, this);
    lv_obj_align(scrim, LV_ALIGN_CENTER, 0, 0);
}

void FilesView::refresh_selection() noexcept
{
    if (nav_title_) lv_label_set_text(nav_title_, view_model_.selection_title().c_str());
    if (select_all_label_) {
        lv_label_set_text(select_all_label_, view_model_.all_selected() ? "Deselect All" : "Select All");
    }
    // Changing the selection clears an earlier command failure.
    if (selection_error_ && view_model_.error_text().empty()) lv_obj_add_flag(selection_error_, LV_OBJ_FLAG_HIDDEN);
    apply_toolbar_state();
    if (!entry_grid_) return;
    const std::uint32_t count = lv_obj_get_child_count(entry_grid_);
    for (std::uint32_t index = 0; index < count; ++index) {
        lv_obj_t *item = lv_obj_get_child(entry_grid_, static_cast<std::int32_t>(index));
        auto *binding  = static_cast<const RowBinding *>(lv_obj_get_user_data(item));
        if (binding) apply_selection_state(item, view_model_.is_selected(binding->index));
    }
}

void FilesView::build_home()
{
    auto *scroll = create_scroller();
    create_navbar(view_model_.directory_name().c_str(), nullptr, nullptr, false, true);
    create_directory_content(scroll);
}

void FilesView::build_folder()
{
    auto *scroll = create_scroller();
    create_navbar(view_model_.directory_name().c_str(), view_model_.display_path().c_str(), "files_path_label", true,
                  true);
    create_directory_content(scroll);
}

void FilesView::build_select()
{
    // iOS selection mode: Select All on the left, the selection count as the title and Done on the right.
    auto *scroll      = create_scroller();
    auto *bar         = create_navbar_row();
    auto *select_all  = create_capsule_button(bar, view_model_.all_selected() ? "Deselect All" : "Select All",
                                              "files_select_all_button", select_all_clicked);
    select_all_label_ = lv_obj_get_child(select_all, 0);
    nav_title_        = create_nav_titles(bar, view_model_.selection_title().c_str(), nullptr, nullptr);

    auto *done = create_circle_button(bar, LV_SYMBOL_OK, "files_select_done", back_clicked);
    lv_obj_set_style_bg_color(done, lv_color_hex(components::color_action), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(done, 0), lv_color_hex(components::color_on_action), 0);

    // A failed toolbar command explains why above the content; menus and prompts over selection mode
    // show their own errors.
    if (view_model_.screen() == FilesScreen::Select && !view_model_.error_text().empty()) {
        selection_error_ = create_label(chrome_, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(selection_error_, "files_select_error");
        lv_obj_set_width(selection_error_, LV_PCT(100));
        lv_obj_set_style_pad_hor(selection_error_, 8, 0);
        lv_obj_set_style_text_align(selection_error_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(selection_error_, LV_LABEL_LONG_WRAP);
    }
    create_directory_content(scroll);
    create_selection_toolbar();
}

void FilesView::build_search()
{
    // Like iOS search mode, the field takes the title's place so results keep the height above the keyboard.
    auto *scroll = create_scroller();
    auto *bar    = create_navbar_row();
    create_circle_button(bar, LV_SYMBOL_LEFT, "files_back_button", back_clicked);
    auto *field = create_search_field(bar, view_model_.query().c_str(), true);
    lv_obj_set_width(field, 0);
    lv_obj_set_flex_grow(field, 1);

    auto *toolbar = create_box(chrome_);
    lv_obj_set_name_static(toolbar, "files_search_toolbar");
    lv_obj_set_width(toolbar, LV_PCT(100));
    lv_obj_set_height(toolbar, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(toolbar, 8, 0);
    lv_obj_set_style_pad_column(toolbar, 12, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    search_count_ = create_label(toolbar, "", 14, components::color_muted_text);
    lv_obj_set_name_static(search_count_, "files_result_count");
    auto *path = create_label(toolbar, view_model_.display_path().c_str(), 14, components::color_muted_text);
    lv_obj_set_name_static(path, "files_path_label");
    lv_obj_set_width(path, 0);
    lv_obj_set_flex_grow(path, 1);
    lv_obj_set_style_text_align(path, LV_TEXT_ALIGN_RIGHT, 0);
    limit_lines(path, 14, 1);

    if (!view_model_.error_text().empty()) {
        search_error_ = create_label(chrome_, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(search_error_, "files_search_error");
        lv_obj_set_width(search_error_, LV_PCT(100));
        lv_obj_set_style_pad_hor(search_error_, 8, 0);
        lv_label_set_long_mode(search_error_, LV_LABEL_LONG_WRAP);
    }

    search_results_ = create_entry_collection(scroll, true, false);
    keyboard_       = create_keyboard(base_, "files_keyboard", search_input_, 260);
    lv_obj_set_style_max_height(keyboard_, kKeyboardMaximumHeight, 0);
    refresh_search_results();
}

void FilesView::build_preview()
{
    const FilesEntryPresentation *entry = view_model_.preview_entry();
    const char *name                    = entry ? entry->name.c_str() : "File";
    const char *metadata                = entry ? entry->summary.c_str() : "";
    const char *tag_text                = entry && !entry->type_label.empty() ? entry->type_label.c_str() : "FILE";

    auto *scroll = create_scroller();
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    create_navbar(name, metadata, "files_preview_metadata", true, false);

    auto *document = components::create_card(scroll, 20);
    lv_obj_set_name_static(document, "files_preview_document");
    lv_obj_set_width(document, LV_PCT(100));
    lv_obj_set_style_max_width(document, kPreviewMaximumWidth, 0);
    lv_obj_set_height(document, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(document, 0, 0);
    lv_obj_set_style_pad_all(document, 28, 0);
    lv_obj_set_style_pad_row(document, 18, 0);
    lv_obj_set_flex_flow(document, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(document, LV_OBJ_FLAG_SCROLLABLE);

    auto *tag = create_box(document);
    lv_obj_set_size(tag, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_min_width(tag, 56, 0);
    lv_obj_set_style_pad_hor(tag, 12, 0);
    lv_obj_set_style_bg_color(tag, lv_color_hex(components::color_selected_background), 0);
    lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tag, LV_RADIUS_CIRCLE, 0);
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
    create_scrim(LV_OPA_TRANSP);

    // iOS pull-down menu: actions, view modes and sort keys separated by dividers. A tap outside closes it.
    auto *menu = create_menu_card("files_more_menu");
    lv_obj_align(menu, LV_ALIGN_TOP_RIGHT, -kRegularPadding, kMenuTop);
    create_menu_error(menu);

    create_menu_item(menu, "Select", nullptr, kSelectSymbol, false, "files_menu_select", select_mode_clicked, this);
    create_menu_item(menu, "New Folder", nullptr, kNewFolderSymbol, false, "files_new_folder_button",
                     new_folder_clicked, this);
    if (view_model_.has_clipboard()) {
        create_menu_item(menu, "Paste", nullptr, kPasteSymbol, false, "files_menu_paste", paste_clicked, this);
    }
    create_menu_divider(menu);

    const FilesViewMode mode = view_model_.view_mode();
    create_menu_item(menu, "Icons", nullptr, kIconsSymbol, mode == FilesViewMode::Icons, "files_view_icons",
                     view_mode_clicked, bind_menu(static_cast<std::int32_t>(FilesViewMode::Icons)));
    create_menu_item(menu, "List", nullptr, kListSymbol, mode == FilesViewMode::List, "files_view_list",
                     view_mode_clicked, bind_menu(static_cast<std::int32_t>(FilesViewMode::List)));
    create_menu_divider(menu);

    struct SortItem {
        FilesSortKey key;
        const char *text;
        const char *name;
    };
    static constexpr SortItem sort_items[] = {
        {FilesSortKey::Name, "Name", "files_sort_name"},
        {FilesSortKey::Kind, "Kind", "files_sort_kind"},
        {FilesSortKey::Date, "Date", "files_sort_date"},
        {FilesSortKey::Size, "Size", "files_sort_size"},
    };
    for (const SortItem &item : sort_items) {
        // The active key shows its direction; choosing it again reverses the order.
        const bool active     = view_model_.sort_key() == item.key;
        const char *direction = view_model_.sort_ascending() ? "Ascending" : "Descending";
        create_menu_item(menu, item.text, active ? direction : nullptr, nullptr, active, item.name, sort_clicked,
                         bind_menu(static_cast<std::int32_t>(item.key)));
    }
}

void FilesView::build_actions()
{
    create_underlay();
    create_scrim(LV_OPA_30);

    // The long-pressed item is lifted above the dimmed content, as in the iOS context menu.
    if (const FilesEntryPresentation *entry = view_model_.action_entry()) {
        action_preview_ = view_model_.view_mode() == FilesViewMode::List ? create_list_row(page_, *entry, false)
                                                                         : create_grid_cell(page_, *entry, false);
        lv_obj_set_name_static(action_preview_, "files_action_preview");
        lv_obj_add_flag(action_preview_, LV_OBJ_FLAG_FLOATING);
        lv_obj_clear_flag(action_preview_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(action_preview_, lv_color_hex(components::color_container_background), 0);
        lv_obj_set_style_bg_opa(action_preview_, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(action_preview_, 24, 0);
        lv_obj_set_style_shadow_offset_y(action_preview_, 4, 0);
        lv_obj_set_style_shadow_color(action_preview_, lv_color_hex(components::color_shadow), 0);
        lv_obj_set_style_shadow_opa(action_preview_, LV_OPA_10, 0);
    }

    // Copy and Move lead as tiles, then the item commands and Delete. Share, tags and favorites are not offered.
    auto *menu = create_menu_card("files_action_menu");
    create_menu_error(menu);
    auto *tiles = create_box(menu);
    lv_obj_set_name_static(tiles, "files_action_tiles");
    lv_obj_set_size(tiles, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(tiles, 8, 0);
    lv_obj_set_style_pad_column(tiles, 4, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
    create_action_tile(tiles, "Copy", kCopySymbol, "files_action_copy", static_cast<std::int32_t>(ItemAction::Copy));
    create_action_tile(tiles, "Move", kMoveSymbol, "files_action_move", static_cast<std::int32_t>(ItemAction::Move));
    create_menu_divider(menu);

    struct ActionItem {
        ItemAction action;
        const char *text;
        const char *symbol;
        const char *name;
    };
    static constexpr ActionItem action_items[] = {
        {ItemAction::Info, "Get Info", kInfoSymbol, "files_action_info"},
        {ItemAction::Rename, "Rename", kRenameSymbol, "files_action_rename"},
        {ItemAction::Compress, "Compress", kCompressSymbol, "files_action_compress"},
        {ItemAction::Duplicate, "Duplicate", kDuplicateSymbol, "files_action_duplicate"},
        {ItemAction::NewFolder, "New Folder with 1 Item", kNewFolderSymbol, "files_action_new_folder"},
    };
    for (const ActionItem &item : action_items) {
        create_action_item(menu, item.text, item.symbol, item.name, static_cast<std::int32_t>(item.action), false);
    }
    create_menu_divider(menu);
    create_action_item(menu, "Delete", kDeleteSymbol, "files_action_delete",
                       static_cast<std::int32_t>(ItemAction::Delete), true);
}

void FilesView::build_selection_actions()
{
    create_underlay();
    create_scrim(LV_OPA_TRANSP);

    // The selection toolbar's menu, after iOS without tags: copy the items, put them in a new folder or
    // compress them. A tap outside closes it and keeps the selection.
    auto *menu = create_menu_card("files_selection_menu");
    create_menu_error(menu);
    create_action_item(menu, view_model_.copy_items_title().c_str(), kCopySymbol, "files_selection_copy",
                       static_cast<std::int32_t>(ItemAction::Copy), false);
    create_action_item(menu, view_model_.new_folder_with_items_title().c_str(), kNewFolderSymbol,
                       "files_selection_new_folder", static_cast<std::int32_t>(ItemAction::NewFolder), false);
    create_action_item(menu, "Compress", kCompressSymbol, "files_selection_compress",
                       static_cast<std::int32_t>(ItemAction::Compress), false);
}

void FilesView::build_info()
{
    // Get Info after the iOS Files sheet: the item with an Open button, then its information list.
    auto *scroll = create_scroller();
    lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *bar = create_navbar_row();
    create_circle_button(bar, LV_SYMBOL_CLOSE, "files_info_close", back_clicked);
    create_nav_titles(bar, "Info", nullptr, nullptr);
    lv_obj_set_size(create_box(bar), kNavButtonSize, kNavButtonSize);

    const FilesInfoPresentation *info = view_model_.info();
    if (!info) return;
    auto *content = create_box(scroll);
    lv_obj_set_name_static(content, "files_info_content");
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(content, kPreviewMaximumWidth, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    files_icons::create(content, info->icon_kind, info->type_label, kInfoIconHeight);
    auto *name = create_label(content, info->name.c_str(), 28, components::color_container_title);
    lv_obj_set_name_static(name, "files_info_name");
    lv_obj_set_width(name, LV_PCT(100));
    lv_obj_set_style_pad_top(name, 12, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    auto *kind = create_label(content, info->kind.c_str(), 22, components::color_muted_text);
    lv_obj_set_name_static(kind, "files_info_kind");

    if (info->can_open) {
        auto *open = components::create_button(content, components::color_action, LV_RADIUS_CIRCLE);
        lv_obj_set_name_static(open, "files_info_open");
        lv_obj_set_size(open, LV_SIZE_CONTENT, 48);
        lv_obj_set_style_pad_hor(open, 28, 0);
        lv_obj_set_style_margin_top(open, 8, 0);
        auto *text = create_label(open, "Open", 22, components::color_on_action);
        lv_obj_center(text);
        lv_obj_add_event_cb(open, info_open_clicked, LV_EVENT_CLICKED, this);
    }
    if (!view_model_.error_text().empty()) {
        auto *error = create_label(content, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(error, "files_info_error");
        lv_obj_set_width(error, LV_PCT(100));
        lv_label_set_long_mode(error, LV_LABEL_LONG_WRAP);
    }

    auto *heading = create_label(content, "Information", 22, components::color_muted_text);
    lv_obj_set_style_pad_top(heading, 16, 0);
    auto *list = components::create_card(content, kInfoCardRadius);
    lv_obj_set_name_static(list, "files_info_list");
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_hor(list, 20, 0);
    lv_obj_set_style_pad_ver(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    create_info_row(list, "Kind", info->kind, "files_info_kind_value");
    create_info_row(list, "Size", info->size, "files_info_size_value");
    create_info_row(list, "Created", info->created, "files_info_created_value");
    create_info_row(list, "Modified", info->modified, "files_info_modified_value");
    create_info_row(list, "Where", info->location, "files_info_location_value");
}

void FilesView::build_move()
{
    // The iOS Move sheet: browse to a folder, then move the item there.
    auto *scroll = create_scroller();
    auto *bar    = create_navbar_row();
    create_circle_button(bar, LV_SYMBOL_CLOSE, "files_move_cancel", back_clicked);
    create_nav_titles(bar, view_model_.move_title().c_str(), view_model_.move_display_path().c_str(),
                      "files_move_path");
    auto *confirm      = create_capsule_button(bar, "Move", "files_move_confirm", move_confirm_clicked);
    const bool enabled = view_model_.can_move_here();
    lv_obj_set_style_bg_color(
        confirm, lv_color_hex(enabled ? components::color_action : components::color_secondary_control), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(confirm, 0),
                                lv_color_hex(enabled ? components::color_on_action : components::color_muted_text), 0);
    if (!enabled) lv_obj_add_state(confirm, LV_STATE_DISABLED);

    auto *hint = create_label(scroll, view_model_.move_hint().c_str(), 14, components::color_muted_text);
    lv_obj_set_name_static(hint, "files_move_hint");
    lv_obj_set_width(hint, LV_PCT(100));
    limit_lines(hint, 14, 2);
    if (!view_model_.error_text().empty()) {
        auto *error = create_label(scroll, view_model_.error_text().c_str(), 14, components::color_destructive);
        lv_obj_set_name_static(error, "files_move_error");
        lv_obj_set_width(error, LV_PCT(100));
        lv_label_set_long_mode(error, LV_LABEL_LONG_WRAP);
    }

    auto *list = create_box(scroll);
    lv_obj_set_name_static(list, "files_move_list");
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    if (!view_model_.move_at_root()) {
        // Returns to the enclosing folder, labelled with its name like an iOS back button.
        auto *up = components::create_button(list, components::color_page_background, kCellRadius);
        lv_obj_set_name_static(up, "files_move_up");
        lv_obj_set_size(up, LV_PCT(100), kMenuItemHeight);
        lv_obj_set_style_bg_opa(up, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(up, lv_color_hex(components::color_secondary_control), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(up, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_pad_column(up, 12, 0);
        lv_obj_set_flex_flow(up, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(up, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto *chevron = create_label(up, LV_SYMBOL_LEFT, 22, components::color_navigation_accent);
        lv_obj_set_width(chevron, kListLeadingWidth);
        lv_obj_set_style_text_align(chevron, LV_TEXT_ALIGN_CENTER, 0);
        auto *parent =
            create_label(up, view_model_.move_parent_name().c_str(), 22, components::color_navigation_accent);
        lv_obj_set_width(parent, 0);
        lv_obj_set_flex_grow(parent, 1);
        limit_lines(parent, 22, 1);
        lv_obj_add_event_cb(up, move_up_clicked, LV_EVENT_CLICKED, this);
    }

    const std::size_t count = view_model_.move_destination_count();
    for (std::size_t index = 0; index < count; ++index) {
        const FilesEntryPresentation *entry = view_model_.move_destination(index);
        if (!entry) continue;
        auto *row = create_entry_item(list, *entry, index, RowSource::MoveDestination);
        lv_obj_set_name_static(row, "files_move_destination");
        // The folder being moved cannot contain itself.
        if (!view_model_.move_destination_enabled(index)) {
            lv_obj_add_state(row, LV_STATE_DISABLED);
            lv_obj_set_style_opa(row, LV_OPA_40, 0);
        }
    }
    if (count == 0) {
        auto *empty = create_label(scroll, "No folders", 22, components::color_muted_text);
        lv_obj_set_name_static(empty, "files_move_empty");
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_top(empty, 32, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void FilesView::build_name_dialog()
{
    // New Folder and Rename share one alert: a title, a hint or error, the name field and two capsules.
    const FilesScreen screen = view_model_.screen();
    const bool renaming      = screen == FilesScreen::Rename;
    create_underlay();
    create_scrim(LV_OPA_30);

    auto *dialog = create_dialog(renaming ? "files_rename_dialog" : "files_new_folder_dialog");
    auto *title  = create_label(dialog, renaming ? "Rename" : "New Folder", 28, components::color_container_title);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    const bool has_error = !view_model_.error_text().empty();
    const char *hint     = has_error ? view_model_.error_text().c_str() : name_dialog_hint(screen);
    name_hint_ =
        create_label(dialog, hint, 14, has_error ? components::color_destructive : components::color_muted_text);
    lv_obj_set_name_static(name_hint_, renaming ? "files_rename_hint" : "files_folder_hint");
    lv_obj_set_width(name_hint_, LV_PCT(100));
    lv_label_set_long_mode(name_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(name_hint_, LV_TEXT_ALIGN_CENTER, 0);

    auto *input = lv_textarea_create(dialog);
    lv_obj_set_name_static(input, renaming ? "files_rename_input" : "files_folder_name_input");
    lv_obj_set_width(input, LV_PCT(100));
    lv_textarea_set_one_line(input, true);
    lv_obj_set_height(input, 56);
    lv_textarea_set_max_length(input, (renaming ? FilesModel::name_capacity : FilesViewModel::text_capacity) - 1U);
    lv_textarea_set_text(input, renaming ? view_model_.rename_name().c_str() : view_model_.folder_name().c_str());
    (void)components::set_text_font(input, 22);
    lv_obj_set_style_text_color(input, lv_color_hex(components::color_text), 0);
    lv_obj_set_style_bg_color(input, lv_color_hex(components::color_control_background), 0);
    lv_obj_set_style_border_width(input, 2, 0);
    lv_obj_set_style_border_color(input, lv_color_hex(components::color_control_background), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(components::color_navigation_accent), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(input, 14, 0);
    lv_obj_set_style_pad_hor(input, 16, 0);
    lv_obj_add_event_cb(input, name_changed, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(input, search_input_activated, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(input, search_input_activated, LV_EVENT_CLICKED, this);
    name_input_ = input;

    auto *actions = create_box(dialog);
    lv_obj_set_size(actions, LV_PCT(100), kDialogButtonHeight);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    create_dialog_button(actions, "Cancel", renaming ? "files_rename_cancel" : "files_folder_cancel",
                         components::color_secondary_control, components::color_text, back_clicked);
    create_dialog_button(actions, renaming ? "Rename" : "Create",
                         renaming ? "files_rename_confirm" : "files_folder_create", components::color_action,
                         components::color_on_action, name_confirm_clicked);

    keyboard_ = create_keyboard(page_, renaming ? "files_rename_keyboard" : "files_folder_keyboard", name_input_, 220);
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_max_height(keyboard_, 260, 0);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void FilesView::build_delete()
{
    create_underlay();
    create_scrim(LV_OPA_30);

    // Deleting is permanent, so it asks first; Delete is the destructive choice.
    auto *dialog  = create_dialog("files_delete_dialog");
    auto *heading = create_label(dialog, view_model_.delete_title().c_str(), 28, components::color_container_title);
    lv_obj_set_name_static(heading, "files_delete_title");
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    limit_lines(heading, 28, 3);

    const bool has_error   = !view_model_.error_text().empty();
    const std::string text = has_error ? view_model_.error_text() : view_model_.delete_message();
    auto *message          = create_label(dialog, text.c_str(), 14,
                                 has_error ? components::color_destructive : components::color_muted_text);
    lv_obj_set_name_static(message, "files_delete_message");
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);

    auto *actions = create_box(dialog);
    lv_obj_set_size(actions, LV_PCT(100), kDialogButtonHeight);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    create_dialog_button(actions, "Cancel", "files_delete_cancel", components::color_secondary_control,
                         components::color_text, back_clicked);
    create_dialog_button(actions, "Delete", "files_delete_confirm", components::color_destructive,
                         components::color_on_action, delete_confirm_clicked);
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
        lv_obj_set_style_pad_top(empty, 32, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (std::size_t index = 0; index < result_count; ++index) {
        const auto *entry = view_model_.search_entry(index);
        if (entry) create_entry_item(search_results_, *entry, index, RowSource::Search);
    }
}
