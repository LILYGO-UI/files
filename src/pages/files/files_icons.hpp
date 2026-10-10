#ifndef LILYGO_UI_FILES_PAGES_FILES_FILES_ICONS_HPP
#define LILYGO_UI_FILES_PAGES_FILES_FILES_ICONS_HPP

#include "pages/files/files_model.hpp"

#include <lvgl.h>

#include <cstdint>
#include <string_view>

namespace files_icons {

inline constexpr std::int32_t grid_height = 96;
inline constexpr std::int32_t list_height = 56;

// Width of the icon box for a given icon height; the drawn shape is centered and bottom aligned in it.
[[nodiscard]] std::int32_t box_width(std::int32_t height) noexcept;
// Width of the drawn folder or document shape.
[[nodiscard]] std::int32_t shape_width(FilesEntryKind kind, std::int32_t height) noexcept;

// Creates a decorative entry icon: an iOS-style folder, or a document page with a folded corner
// and its short type label. The icon never takes clicks away from the cell that contains it.
lv_obj_t *create(lv_obj_t *parent, FilesEntryKind kind, std::string_view type_label, std::int32_t height = grid_height);

}  // namespace files_icons

#endif
