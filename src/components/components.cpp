#include "components/components.hpp"

#include <cm0/typography.h>

namespace components {

const lv_font_t *font(std::uint32_t size) noexcept
{
    if (size == 0) {
        LV_LOG_ERROR("AppKit font size must be positive");
        return nullptr;
    }

    const auto *result = lilygo_ui_font_get(size);
    if (!result) LV_LOG_ERROR("AppKit font is unavailable at %u px", size);
    return result;
}

bool set_text_font(lv_obj_t *object, std::uint32_t size, lv_style_selector_t selector) noexcept
{
    if (!object) return false;
    const auto *requested_font = font(size);
    if (!requested_font) return false;
    lv_obj_set_style_text_font(object, requested_font, selector);
    return true;
}

lv_obj_t *create_page(lv_obj_t *parent)
{
    if (!parent) return nullptr;
    auto *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page, lv_color_hex(color_page_background), 0);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

lv_obj_t *create_card(lv_obj_t *parent, std::int32_t radius)
{
    if (!parent) return nullptr;
    auto *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, lv_color_hex(color_container_background), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(color_border), 0);
    lv_obj_set_style_radius(card, radius, 0);
    return card;
}

lv_obj_t *create_button(lv_obj_t *parent, std::uint32_t background, std::int32_t radius)
{
    if (!parent) return nullptr;
    auto *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, radius, 0);
    return button;
}

}  // namespace components
