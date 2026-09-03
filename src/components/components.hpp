#ifndef LILYGO_UI_FILES_COMPONENTS_COMPONENTS_HPP
#define LILYGO_UI_FILES_COMPONENTS_COMPONENTS_HPP

#include <lvgl.h>

#include <cstdint>

namespace components {

inline constexpr std::uint32_t color_page_background      = 0xf2f2f7;
inline constexpr std::uint32_t color_container_background = 0xffffff;
inline constexpr std::uint32_t color_container_title      = 0x1a1a1a;
inline constexpr std::uint32_t color_text                 = 0x000000;
inline constexpr std::uint32_t color_border               = 0xd8dde3;
inline constexpr std::uint32_t color_action               = 0x20262d;
inline constexpr std::uint32_t color_on_action            = 0xffffff;
inline constexpr std::uint32_t color_navigation_accent    = 0x007aff;
inline constexpr std::uint32_t color_muted_text           = 0x8e8e93;
inline constexpr std::uint32_t color_placeholder_text     = 0x7c7c80;
inline constexpr std::uint32_t color_selected_background  = 0xeaf4ff;
inline constexpr std::uint32_t color_control_background   = 0xe3e3e8;
inline constexpr std::uint32_t color_secondary_control    = 0xe5e5ea;
inline constexpr std::uint32_t color_destructive          = 0xff3b30;
inline constexpr std::uint32_t color_download_accent      = 0x30b0c7;
inline constexpr std::uint32_t color_scrim                = 0x000000;

[[nodiscard]] const lv_font_t *font(std::uint32_t size) noexcept;
[[nodiscard]] bool set_text_font(lv_obj_t *object, std::uint32_t size, lv_style_selector_t selector = 0) noexcept;

lv_obj_t *create_page(lv_obj_t *parent);
lv_obj_t *create_card(lv_obj_t *parent, std::int32_t radius = 8);
lv_obj_t *create_button(lv_obj_t *parent, std::uint32_t background, std::int32_t radius = 8);

}  // namespace components

#endif
