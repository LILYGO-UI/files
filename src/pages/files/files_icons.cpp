#include "pages/files/files_icons.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace files_icons {
namespace {

// Dimensions of the 96 px design; other icon heights scale them proportionally.
constexpr std::int32_t kDesignHeight     = 96;
constexpr std::int32_t kBoxWidth         = 112;
constexpr std::int32_t kFolderWidth      = 104;
constexpr std::int32_t kFolderHeight     = 80;
constexpr std::int32_t kFolderTabWidth   = 42;
constexpr std::int32_t kFolderTabHeight  = 24;
constexpr std::int32_t kFolderBackTop    = 8;
constexpr std::int32_t kFolderFrontTop   = 18;
constexpr std::int32_t kFolderRadius     = 10;
constexpr std::int32_t kFolderHighlight  = 2;
constexpr std::int32_t kDocumentWidth    = 74;
constexpr std::int32_t kDocumentFold     = 22;
constexpr std::int32_t kDocumentRadius   = 8;
constexpr std::int32_t kThumbnailInset   = 12;
constexpr std::int32_t kThumbnailTop     = 32;
constexpr std::int32_t kThumbnailSpacing = 9;
constexpr std::int32_t kThumbnailGap     = 8;
constexpr std::int32_t kBadgeSize        = 40;
constexpr std::int32_t kBadgeTop         = 26;
constexpr std::int32_t kBadgeRadius      = 12;
constexpr std::int32_t kTypeLabelBottom  = 8;
constexpr std::int32_t kLargeGlyphBadge  = 32;
constexpr std::int32_t kDetailedHeight   = 80;

std::int32_t scaled(std::int32_t value, std::int32_t height) noexcept
{
    return std::max<std::int32_t>(1, (value * height + kDesignHeight / 2) / kDesignHeight);
}

// The draw callback receives the kind and height packed into its user data.
void *pack(FilesEntryKind kind, std::int32_t height) noexcept
{
    return reinterpret_cast<void *>((static_cast<std::intptr_t>(height) << 8) | static_cast<std::intptr_t>(kind));
}

// lv_point_precise_t holds floats when the GPU draw units enable LV_USE_FLOAT.
lv_point_precise_t point(std::int32_t x, std::int32_t y)
{
    return {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(y)};
}

void fill(lv_layer_t *layer, std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, std::int32_t radius,
          std::uint32_t color)
{
    lv_draw_fill_dsc_t dsc;
    lv_draw_fill_dsc_init(&dsc);
    dsc.radius = radius;
    dsc.color  = lv_color_hex(color);
    dsc.opa    = LV_OPA_COVER;
    const lv_area_t area{x1, y1, x2, y2};
    lv_draw_fill(layer, &dsc, &area);
}

void draw_folder(lv_layer_t *layer, const lv_area_t &area, std::int32_t height)
{
    const std::int32_t radius    = scaled(kFolderRadius, height);
    const std::int32_t back_top  = area.y1 + scaled(kFolderBackTop, height);
    const std::int32_t front_top = area.y1 + scaled(kFolderFrontTop, height);

    // The tab peeks above the back panel; the lighter front panel covers both with a top highlight.
    fill(layer, area.x1, area.y1, area.x1 + scaled(kFolderTabWidth, height), area.y1 + scaled(kFolderTabHeight, height),
         std::max<std::int32_t>(1, radius / 2), components::color_folder_back);
    fill(layer, area.x1, back_top, area.x2, area.y2, radius, components::color_folder_back);
    fill(layer, area.x1, front_top, area.x2, area.y2, radius, components::color_folder_front);

    lv_draw_border_dsc_t highlight;
    lv_draw_border_dsc_init(&highlight);
    highlight.radius = radius;
    highlight.color  = lv_color_hex(components::color_folder_highlight);
    highlight.width  = scaled(kFolderHighlight, height);
    highlight.opa    = LV_OPA_COVER;
    highlight.side   = LV_BORDER_SIDE_TOP;
    const lv_area_t front{area.x1, front_top, area.x2, area.y2};
    lv_draw_border(layer, &highlight, &front);
}

void draw_document(lv_layer_t *layer, const lv_area_t &area, std::int32_t height, bool thumbnail)
{
    const std::int32_t fold   = scaled(kDocumentFold, height);
    const std::int32_t fold_x = area.x2 - fold;
    const std::int32_t fold_y = area.y1 + fold;
    const std::int32_t radius = scaled(kDocumentRadius, height);

    // The page is the union of a left body, a lower-right body and a square that removes the left body's
    // rounded corner at the fold. The outline is the same silhouette grown by one pixel.
    fill(layer, area.x1 - 1, area.y1 - 1, fold_x + 1, area.y2 + 1, radius + 1, components::color_border);
    fill(layer, fold_x - 2 * radius - 1, fold_y - 1, area.x2 + 1, area.y2 + 1, radius + 1, components::color_border);
    fill(layer, fold_x - radius - 1, area.y1 - 1, fold_x, area.y1 + radius, 0, components::color_border);
    fill(layer, area.x1, area.y1, fold_x, area.y2, radius, components::color_container_background);
    fill(layer, fold_x - 2 * radius, fold_y, area.x2, area.y2, radius, components::color_container_background);
    fill(layer, fold_x - radius, area.y1, fold_x, area.y1 + radius, 0, components::color_container_background);

    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.p[0]  = point(fold_x, area.y1);
    triangle.p[1]  = point(fold_x, fold_y);
    triangle.p[2]  = point(area.x2, fold_y);
    triangle.color = lv_color_hex(components::color_document_fold);
    triangle.opa   = LV_OPA_COVER;
    lv_draw_triangle(layer, &triangle);

    lv_draw_line_dsc_t edge;
    lv_draw_line_dsc_init(&edge);
    edge.p1    = point(fold_x, area.y1);
    edge.p2    = point(area.x2, fold_y);
    edge.color = lv_color_hex(components::color_border);
    edge.width = 1;
    edge.opa   = LV_OPA_COVER;
    lv_draw_line(layer, &edge);

    if (!thumbnail) return;
    // A few text lines suggest the document contents, like the thumbnails in iOS Files.
    const bool detailed           = height >= kDetailedHeight;
    const std::int32_t thickness  = detailed ? 3 : 2;
    const std::int32_t inset      = scaled(kThumbnailInset, height);
    const std::int32_t left       = area.x1 + inset;
    const std::int32_t right      = area.x2 - inset;
    const std::int32_t line_count = detailed ? 4 : 2;
    fill(layer, left, area.y1 + inset, fold_x - scaled(kThumbnailGap, height), area.y1 + inset + thickness, 1,
         components::color_muted_text);
    for (std::int32_t line = 0; line < line_count; ++line) {
        const std::int32_t top = area.y1 + scaled(kThumbnailTop, height) + line * scaled(kThumbnailSpacing, height);
        const std::int32_t end = line + 1 == line_count ? left + (right - left) * 3 / 5 : right;
        fill(layer, left, top, end, top + thickness - 1, 1, components::color_border);
    }
}

void draw_icon(lv_event_t *event)
{
    auto *object      = lv_event_get_current_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    if (!object || !layer) return;
    const auto packed         = reinterpret_cast<std::intptr_t>(lv_event_get_user_data(event));
    const auto kind           = static_cast<FilesEntryKind>(packed & 0xff);
    const std::int32_t height = static_cast<std::int32_t>(packed >> 8);

    lv_area_t coords{};
    lv_obj_get_coords(object, &coords);
    const std::int32_t width = shape_width(kind, height);
    const std::int32_t left  = (coords.x1 + coords.x2 + 1 - width) / 2;
    if (kind == FilesEntryKind::Folder) {
        const lv_area_t area{left, coords.y2 - scaled(kFolderHeight, height) + 1, left + width - 1, coords.y2};
        draw_folder(layer, area, height);
        return;
    }

    // Keep one pixel free on every side so the outline stays inside the object's draw area.
    const lv_area_t area{left, coords.y1 + 1, left + width - 1, coords.y2 - 1};
    draw_document(layer, area, height, kind == FilesEntryKind::Text || kind == FilesEntryKind::Pdf);
}

lv_obj_t *create_plain(lv_obj_t *parent)
{
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

void create_audio_badge(lv_obj_t *icon, std::int32_t height)
{
    const std::int32_t size = scaled(kBadgeSize, height);
    auto *badge             = create_plain(icon);
    lv_obj_set_size(badge, size, size);
    lv_obj_set_style_radius(badge, scaled(kBadgeRadius, height), 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(components::color_type_audio), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, scaled(kBadgeTop, height));

    auto *glyph = lv_label_create(badge);
    lv_label_set_text(glyph, LV_SYMBOL_AUDIO);
    (void)components::set_text_font(glyph, size >= kLargeGlyphBadge ? 22 : 14);
    lv_obj_set_style_text_color(glyph, lv_color_hex(components::color_on_action), 0);
    lv_obj_center(glyph);
}

}  // namespace

std::int32_t box_width(std::int32_t height) noexcept
{
    return scaled(kBoxWidth, height);
}

std::int32_t shape_width(FilesEntryKind kind, std::int32_t height) noexcept
{
    return scaled(kind == FilesEntryKind::Folder ? kFolderWidth : kDocumentWidth, height);
}

lv_obj_t *create(lv_obj_t *parent, FilesEntryKind kind, std::string_view type_label, std::int32_t height)
{
    auto *icon = create_plain(parent);
    lv_obj_set_size(icon, box_width(height), height);
    lv_obj_add_event_cb(icon, draw_icon, LV_EVENT_DRAW_MAIN, pack(kind, height));
    if (kind == FilesEntryKind::Folder) {
        lv_obj_set_name_static(icon, "files_folder_icon");
        return icon;
    }

    lv_obj_set_name_static(icon, "files_document_icon");
    if (kind == FilesEntryKind::Music) create_audio_badge(icon, height);
    // Small icons have no room for both the badge and the type label; the badge identifies audio.
    const bool badge_only = kind == FilesEntryKind::Music && height < kDetailedHeight;
    if (!type_label.empty() && !badge_only) {
        auto *label = lv_label_create(icon);
        lv_obj_set_name_static(label, "files_type_label");
        lv_label_set_text(label, std::string(type_label).c_str());
        (void)components::set_text_font(label, 14);
        const std::uint32_t color =
            kind == FilesEntryKind::Pdf ? components::color_type_pdf : components::color_muted_text;
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -scaled(kTypeLabelBottom, height));
    }
    return icon;
}

}  // namespace files_icons
