#include "pages/help/help_view.hpp"

#include "components/components.hpp"
#include "pages/help/help_content.hpp"

#include <algorithm>
#include <cstdint>

namespace {
constexpr std::int32_t kNavbarHeight       = 72;
constexpr std::int32_t kNavButtonSize      = 56;
constexpr std::int32_t kNavbarGap          = 12;
constexpr std::int32_t kPagePadding        = 16;
constexpr std::int32_t kContentMaximum     = 860;
constexpr std::int32_t kCardPadding        = 20;
constexpr std::int32_t kCardRadius         = 18;
constexpr std::int32_t kSectionGap         = 16;
constexpr std::int32_t kLabelGap           = 8;
constexpr std::int32_t kBackSymbolSize     = 22;
constexpr std::int32_t kTitleSize          = 28;
constexpr std::int32_t kSectionTitleSize   = 22;
constexpr std::int32_t kBodySize           = 14;
constexpr std::uint32_t kCardTitleColor    = components::color_container_title;
constexpr std::uint32_t kBodyColor         = components::color_text;
constexpr std::uint32_t kBackColor         = components::color_container_background;
constexpr std::uint32_t kPressedColor      = components::color_secondary_control;

HelpView *view_from_event(lv_event_t *event)
{
    return event ? static_cast<HelpView *>(lv_event_get_user_data(event)) : nullptr;
}

}  // namespace

HelpView::HelpView(BackCallback back_callback, void *user_data) noexcept
    : back_callback_(back_callback), user_data_(user_data)
{
}

HelpView::~HelpView() noexcept
{
    destroy();
}

lv_obj_t *HelpView::create(lv_obj_t *parent)
{
    if (!parent) return nullptr;
    destroy();

    surface_ = components::create_page(parent);
    if (!surface_) return nullptr;
    lv_obj_set_name_static(surface_, "help_surface");
    lv_obj_set_flex_flow(surface_, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(surface_, size_changed, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(surface_, key_pressed, LV_EVENT_KEY, this);

    build();
    lv_obj_update_layout(surface_);
    apply_layout();
    return surface_;
}

void HelpView::destroy() noexcept
{
    if (open_reset_timer_) {
        lv_timer_delete(open_reset_timer_);
        open_reset_timer_ = nullptr;
    }
    if (surface_) {
        configure_key_remap(false);
        remove_focus_targets();
        lv_obj_delete(surface_);
    }
    surface_ = nullptr;
    navbar_ = nullptr;
    scroller_ = nullptr;
    content_ = nullptr;
    back_button_ = nullptr;
    focus_targets_.fill(nullptr);
    focus_target_count_ = 0;
}

void HelpView::set_visible(bool visible) noexcept
{
    if (!surface_) return;
    if (visible) {
        if (open_reset_timer_) {
            lv_timer_delete(open_reset_timer_);
            open_reset_timer_ = nullptr;
        }
        configure_key_remap(true);
        lv_obj_clear_flag(surface_, LV_OBJ_FLAG_HIDDEN);
        restore_focus_targets();
        lv_obj_move_foreground(surface_);
        lv_obj_update_layout(surface_);
        apply_layout();
        if (back_button_) {
            if (lv_group_t *group = lv_group_get_default()) {
                if (lv_obj_get_group(back_button_) != group) lv_group_add_obj(group, back_button_);
                lv_group_focus_obj(back_button_);
            }
        }
        // Focus restoration can trigger LVGL's scroll-on-focus handling. Reset
        // after that event so every Help visit starts at the beginning.
        lv_obj_update_layout(surface_);
        apply_layout();
        lv_obj_update_layout(surface_);
        if (scroller_) lv_obj_scroll_to_y(scroller_, 0, LV_ANIM_OFF);
        open_reset_timer_ = lv_timer_create(reset_open_position, 1, this);
        if (open_reset_timer_) lv_timer_set_repeat_count(open_reset_timer_, 1);
    } else {
        if (open_reset_timer_) {
            lv_timer_delete(open_reset_timer_);
            open_reset_timer_ = nullptr;
        }
        configure_key_remap(false);
        remove_focus_targets();
        lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HelpView::set_back_callback(BackCallback callback, void *user_data) noexcept
{
    back_callback_ = callback;
    user_data_     = user_data;
}

void HelpView::back_clicked(lv_event_t *event)
{
    invoke_back(view_from_event(event));
}

void HelpView::key_pressed(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (!view) return;
    const auto key = static_cast<lv_key_t>(lv_event_get_key(event));
    if (view->scroller_ && (key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_HOME || key == LV_KEY_END)) {
        if (key == LV_KEY_HOME) {
            lv_obj_scroll_to_y(view->scroller_, 0, LV_ANIM_OFF);
            if (view->focus_target_count_ > 1) lv_group_focus_obj(view->focus_targets_[1]);
        } else if (key == LV_KEY_END) {
            lv_obj_scroll_to_y(view->scroller_, lv_obj_get_scroll_bottom(view->scroller_), LV_ANIM_OFF);
            if (view->focus_target_count_ > 1) lv_group_focus_obj(view->focus_targets_[view->focus_target_count_ - 1]);
        } else if (view->focus_adjacent(key == LV_KEY_DOWN)) {
            // Moving between the close button and help cards uses the same
            // scroll-on-focus path as Tab. At the content edges the fallback
            // below still lets the user pan the viewport without losing focus.
        } else {
            const std::int32_t step = std::max<std::int32_t>(48, lv_obj_get_content_height(view->scroller_) * 4 / 5);
            lv_obj_scroll_by_bounded(view->scroller_, 0, key == LV_KEY_DOWN ? -step : step, LV_ANIM_OFF);
        }
        lv_event_stop_processing(event);
        return;
    }
    if (key != LV_KEY_ESC && key != LV_KEY_DEL) return;
    invoke_back(view);
    lv_event_stop_processing(event);
    if (lv_indev_t *indev = lv_indev_active()) lv_indev_wait_release(indev);
}

lv_key_t HelpView::remap_key(lv_indev_t *indev, lv_key_t key)
{
    if (key == LV_KEY_ESC && indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
        return LV_KEY_DEL;
    }
    return key;
}

void HelpView::configure_key_remap(bool enabled) noexcept
{
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_key_remap_cb(indev, enabled ? remap_key : nullptr);
        }
    }
}

void HelpView::reset_open_position(lv_timer_t *timer)
{
    auto *view = timer ? static_cast<HelpView *>(lv_timer_get_user_data(timer)) : nullptr;
    if (!view) return;
    view->open_reset_timer_ = nullptr;
    if (!view->surface_ || lv_obj_has_flag(view->surface_, LV_OBJ_FLAG_HIDDEN)) return;

    view->restore_focus_targets();
    if (view->back_button_) {
        if (lv_group_t *group = lv_group_get_default()) lv_group_focus_obj(view->back_button_);
    }
    lv_obj_update_layout(view->surface_);
    view->apply_layout();
    lv_obj_update_layout(view->surface_);
    if (view->scroller_) lv_obj_scroll_to_y(view->scroller_, 0, LV_ANIM_OFF);
}

void HelpView::register_focus_target(lv_obj_t *object) noexcept
{
    if (!object || focus_target_count_ >= focus_targets_.size()) return;

    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_border_width(object, 2, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_border_width(object, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, lv_color_hex(components::color_navigation_accent), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(object, key_pressed, LV_EVENT_KEY, this);
    focus_targets_[focus_target_count_++] = object;
    if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, object);
}

void HelpView::restore_focus_targets() noexcept
{
    if (lv_group_t *group = lv_group_get_default()) {
        for (std::size_t index = 0; index < focus_target_count_; ++index) {
            auto *target = focus_targets_[index];
            if (target && lv_obj_get_group(target) != group) lv_group_add_obj(group, target);
        }
    }
}

void HelpView::remove_focus_targets() noexcept
{
    if (lv_group_t *group = lv_group_get_default()) {
        for (std::size_t index = 0; index < focus_target_count_; ++index) {
            auto *target = focus_targets_[index];
            if (target && lv_obj_get_group(target) == group) lv_group_remove_obj(target);
        }
    }
}

std::size_t HelpView::focus_target_index(lv_obj_t *object) const noexcept
{
    for (std::size_t index = 0; index < focus_target_count_; ++index) {
        if (focus_targets_[index] == object) return index;
    }
    return focus_target_count_;
}

bool HelpView::focus_adjacent(bool forward) noexcept
{
    auto *group = lv_group_get_default();
    if (!group) return false;
    const std::size_t current = focus_target_index(lv_group_get_focused(group));
    if (current >= focus_target_count_) return false;
    const std::size_t next = forward ? current + 1 : (current == 0 ? 0 : current - 1);
    if (next >= focus_target_count_ || next == current) return false;
    lv_group_focus_obj(focus_targets_[next]);
    return true;
}

void HelpView::size_changed(lv_event_t *event)
{
    auto *view = view_from_event(event);
    if (view) view->apply_layout();
}

void HelpView::invoke_back(HelpView *view) noexcept
{
    if (!view || !view->back_callback_) return;
    view->back_callback_(view->user_data_);
}

void HelpView::make_plain(lv_obj_t *object) noexcept
{
    if (!object) return;
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

lv_obj_t *HelpView::create_box(lv_obj_t *parent)
{
    if (!parent) return nullptr;
    auto *box = lv_obj_create(parent);
    make_plain(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

lv_obj_t *HelpView::create_label(lv_obj_t *parent, const char *text, std::uint32_t size, std::uint32_t color)
{
    if (!parent) return nullptr;
    auto *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    (void)components::set_text_font(label, size);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

lv_obj_t *HelpView::create_circle_button(lv_obj_t *parent, const char *symbol, const char *name,
                                         lv_event_cb_t callback, void *user_data)
{
    if (!parent) return nullptr;
    auto *button = components::create_button(parent, kBackColor, LV_RADIUS_CIRCLE);
    if (!button) return nullptr;
    lv_obj_set_name_static(button, name);
    lv_obj_set_size(button, kNavButtonSize, kNavButtonSize);
    lv_obj_set_style_bg_color(button, lv_color_hex(kPressedColor), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 24, 0);
    lv_obj_set_style_shadow_offset_y(button, 4, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(components::color_shadow), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_10, 0);
    auto *icon = create_label(button, symbol, kBackSymbolSize, components::color_container_title);
    if (icon) {
        lv_obj_set_width(icon, LV_SIZE_CONTENT);
        lv_obj_set_height(icon, LV_SIZE_CONTENT);
        lv_obj_center(icon);
    }
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, button);
    return button;
}

void HelpView::build()
{
    if (!surface_) return;

    navbar_ = lv_obj_create(surface_);
    make_plain(navbar_);
    lv_obj_set_name_static(navbar_, "help_navbar");
    lv_obj_set_size(navbar_, LV_PCT(100), kNavbarHeight);
    lv_obj_set_style_pad_column(navbar_, kNavbarGap, 0);
    lv_obj_set_flex_flow(navbar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navbar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(navbar_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    back_button_ = create_circle_button(navbar_, LV_SYMBOL_LEFT, "help_back_button", back_clicked, this);
    register_focus_target(back_button_);

    auto *title = create_label(navbar_, "Help", kTitleSize, components::color_container_title);
    lv_obj_set_name_static(title, "help_nav_title");
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    auto *spacer = create_box(navbar_);
    lv_obj_set_name_static(spacer, "help_nav_spacer");
    lv_obj_set_size(spacer, kNavButtonSize, kNavButtonSize);

    scroller_ = lv_obj_create(surface_);
    make_plain(scroller_);
    lv_obj_set_name_static(scroller_, "help_scroller");
    lv_obj_set_width(scroller_, LV_PCT(100));
    lv_obj_set_flex_grow(scroller_, 1);
    lv_obj_set_style_bg_color(scroller_, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_opa(scroller_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(scroller_, kPagePadding, 0);
    lv_obj_set_style_pad_bottom(scroller_, 28, 0);
    lv_obj_set_style_pad_hor(scroller_, kPagePadding, 0);
    lv_obj_set_style_pad_row(scroller_, kSectionGap, 0);
    lv_obj_set_flex_flow(scroller_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroller_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scroller_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroller_, LV_SCROLLBAR_MODE_AUTO);

    content_ = create_box(scroller_);
    lv_obj_set_name_static(content_, "help_content");
    lv_obj_set_width(content_, LV_PCT(100));
    lv_obj_set_height(content_, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, kSectionGap, 0);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    auto *intro = components::create_card(content_, kCardRadius);
    lv_obj_set_name_static(intro, "help_intro");
    lv_obj_set_width(intro, LV_PCT(100));
    lv_obj_set_height(intro, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(intro, kCardPadding, 0);
    lv_obj_set_style_pad_row(intro, kLabelGap, 0);
    lv_obj_set_flex_flow(intro, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(intro, LV_OBJ_FLAG_SCROLLABLE);
    register_focus_target(intro);
    auto *intro_title = create_label(intro, "Using Files", kSectionTitleSize, kCardTitleColor);
    lv_obj_set_name_static(intro_title, "help_intro_title");
    auto *intro_body = create_label(intro,
                                    "Use the controls below to browse, find, and organize files and folders. All operations stay inside the configured Files root.",
                                    kBodySize, components::color_muted_text);
    lv_obj_set_name_static(intro_body, "help_intro_body");

    for (const auto &section : help_content::sections) {
        auto *card = components::create_card(content_, kCardRadius);
        if (!card) continue;
        lv_obj_set_name_static(card, "help_section");
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(card, kCardPadding, 0);
        lv_obj_set_style_pad_row(card, kLabelGap, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        register_focus_target(card);

        auto *heading = create_label(card, section.title, kSectionTitleSize, kCardTitleColor);
        auto *body    = create_label(card, section.body, kBodySize, kBodyColor);
        if (heading) lv_obj_set_name_static(heading, "help_section_title");
        if (body) lv_obj_set_name_static(body, "help_section_body");
    }
}

void HelpView::apply_layout() noexcept
{
    if (!surface_ || !scroller_ || !content_) return;
    // Flex sizing can expose a stale width until the parent has been laid out;
    // the resolved coordinates are the reliable viewport geometry here.
    lv_area_t scroller_area{};
    lv_obj_get_coords(scroller_, &scroller_area);
    const std::int32_t available = scroller_area.x2 - scroller_area.x1 + 1;
    if (available <= 0) return;

    const std::int32_t horizontal_padding = available >= kContentMaximum + 2 * kPagePadding
                                                 ? (available - kContentMaximum) / 2
                                                 : kPagePadding;
    const std::int32_t content_width = std::max<std::int32_t>(1, available - 2 * horizontal_padding);
    lv_obj_set_style_pad_hor(scroller_, horizontal_padding, 0);
    lv_obj_set_width(content_, content_width);
}
