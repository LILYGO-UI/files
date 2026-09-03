#include "app.hpp"
#include "components/components.hpp"
#include "pages/files/files_model.hpp"

#include "test_fixture.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kPartialRows                   = 80;
constexpr std::string_view kShortPreviewName = "short-note.txt";
constexpr std::string_view kShortPreviewText = "Short preview\nSecond line\n";

std::string long_preview_name()
{
    return "long-preview-" + std::string(160, 'n') + ".txt";
}

std::vector<lv_color32_t> framebuffer;
int framebuffer_stride = 0;
int active_width       = 0;
int active_height      = 0;

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    assert(area->x1 >= 0 && area->y1 >= 0);
    assert(area->x2 < active_width && area->y2 < active_height);
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset      = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(framebuffer_stride) +
                                        static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

bool colors_differ(const lv_color32_t &left, const lv_color32_t &right)
{
    return left.red != right.red || left.green != right.green || left.blue != right.blue || left.alpha != right.alpha;
}

void settle(lv_obj_t *root)
{
    for (int pass = 0; pass < 6; ++pass) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
    lv_obj_update_layout(root);
}

lv_obj_t *require_named(lv_obj_t *root, const char *name)
{
    lv_obj_t *object = lv_obj_find_by_name(root, name);
    assert(object != nullptr);
    return object;
}

struct EntryRowProbe {
    std::string_view entry_name;
    bool match_prefix = false;
    lv_obj_t *row     = nullptr;
};

lv_obj_tree_walk_res_t find_entry_row_cb(lv_obj_t *object, void *user_data)
{
    auto *probe      = static_cast<EntryRowProbe *>(user_data);
    const char *name = lv_obj_get_name(object);
    if (name == nullptr || std::strcmp(name, "files_entry_name") != 0) {
        return LV_OBJ_TREE_WALK_NEXT;
    }

    const std::string_view visible_name(lv_label_get_text(object));
    const bool matches = probe->match_prefix
                             ? visible_name.size() >= probe->entry_name.size() &&
                                   visible_name.compare(0, probe->entry_name.size(), probe->entry_name) == 0
                             : visible_name == probe->entry_name;
    if (!matches) return LV_OBJ_TREE_WALK_NEXT;

    lv_obj_t *row = lv_obj_get_parent(object);
    assert(row != nullptr);
    const char *row_name = lv_obj_get_name(row);
    assert(row_name != nullptr);
    assert(std::strcmp(row_name, "files_entry_row") == 0);
    probe->row = row;
    return LV_OBJ_TREE_WALK_END;
}

lv_obj_t *find_entry_row(lv_obj_t *root, std::string_view entry_name)
{
    EntryRowProbe probe{entry_name, false, nullptr};
    lv_obj_tree_walk(root, find_entry_row_cb, &probe);
    assert(probe.row != nullptr);
    return probe.row;
}

lv_obj_t *find_truncated_entry_row(lv_obj_t *root, std::string_view entry_name, std::string_view visible_prefix)
{
    assert(!visible_prefix.empty());
    EntryRowProbe probe{visible_prefix, true, nullptr};
    lv_obj_tree_walk(root, find_entry_row_cb, &probe);
    assert(probe.row != nullptr);

    auto *label = require_named(probe.row, "files_entry_name");
    assert(lv_label_get_long_mode(label) == LV_LABEL_LONG_DOT);
    assert(std::string_view(lv_label_get_text(label)).size() < entry_name.size());
    return probe.row;
}

void assert_label_starts_with(lv_obj_t *label, std::string_view expected_prefix)
{
    const std::string_view text(lv_label_get_text(label));
    assert(text.size() >= expected_prefix.size());
    assert(text.compare(0, expected_prefix.size(), expected_prefix) == 0);
}

void assert_inside(lv_obj_t *object, lv_obj_t *container)
{
    lv_area_t object_area{};
    lv_area_t container_area{};
    lv_obj_get_coords(object, &object_area);
    lv_obj_get_coords(container, &container_area);
    assert(object_area.x1 >= container_area.x1);
    assert(object_area.y1 >= container_area.y1);
    assert(object_area.x2 <= container_area.x2);
    assert(object_area.y2 <= container_area.y2);
}

void click(lv_obj_t *root, lv_obj_t *object)
{
    assert(object != nullptr);
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
    settle(root);
}

void click(lv_obj_t *root, const char *name)
{
    click(root, require_named(root, name));
}

bool render_has_contrast(lv_display_t *display, int width, int height)
{
    std::fill(framebuffer.begin(), framebuffer.end(), lv_color32_t{});
    lv_obj_invalidate(lv_screen_active());
    lv_tick_inc(20);
    lv_refr_now(display);

    const lv_color32_t first = framebuffer.front();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto &pixel = framebuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(framebuffer_stride) +
                                            static_cast<std::size_t>(x)];
            if (colors_differ(pixel, first)) return true;
        }
    }
    return false;
}

bool write_ppm(const char *path, int width, int height)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto &pixel = framebuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(framebuffer_stride) +
                                            static_cast<std::size_t>(x)];
            output.put(static_cast<char>(pixel.red));
            output.put(static_cast<char>(pixel.green));
            output.put(static_cast<char>(pixel.blue));
        }
    }
    output.close();
    return output.good();
}

void assert_home_shell(lv_obj_t *root)
{
    auto *surface = require_named(root, "files_surface");
    auto *frame   = require_named(root, "files_frame");
    auto *title   = require_named(root, "files_title");
    auto *search  = require_named(root, "files_search_field");
    auto *storage = require_named(root, "files_storage_card");
    require_named(root, "files_more_button");
    assert(std::string_view(lv_label_get_text(title)) == "Files");
    assert(lv_obj_get_width(frame) <= 720);
    assert(
        lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(components::color_page_background)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(search, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(storage, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));
}

void assert_home(lv_obj_t *root)
{
    assert_home_shell(root);
    require_named(root, "files_entry_list");
}

void assert_folder_named(lv_obj_t *root, std::string_view expected_title, std::string_view expected_path,
                         bool has_entries)
{
    require_named(root, "files_navbar");
    require_named(root, "files_back_button");
    auto *title = require_named(root, "files_nav_title");
    auto *path  = require_named(root, "files_path_label");
    if (has_entries) {
        require_named(root, "files_entry_list");
    } else {
        require_named(root, "files_empty_state");
        assert(lv_obj_find_by_name(root, "files_entry_list") == nullptr);
    }
    assert(std::string_view(lv_label_get_text(title)) == expected_title);
    assert(std::string_view(lv_label_get_text(path)) == expected_path);
    assert(lv_label_get_long_mode(path) == LV_LABEL_LONG_DOT);
}

void assert_folder(lv_obj_t *root)
{
    assert_folder_named(root, "Documents", "Documents", true);
}

void assert_search(lv_obj_t *root, std::string_view expected_query)
{
    auto *input    = require_named(root, "files_search_input");
    auto *keyboard = require_named(root, "files_keyboard");
    require_named(root, "files_search_results");
    require_named(root, "files_result_count");
    assert(std::string_view(lv_textarea_get_text(input)) == expected_query);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
}

void assert_preview(lv_obj_t *root, std::string_view expected_name, std::string_view expected_text,
                    bool expect_truncation)
{
    auto *document = require_named(root, "files_preview_document");
    auto *title    = require_named(root, "files_preview_title");
    auto *body     = require_named(root, "files_preview_body");
    require_named(root, "files_preview_metadata");
    const std::string_view body_text(lv_label_get_text(body));
    assert(std::string_view(lv_label_get_text(title)) == expected_name);
    if (expect_truncation) {
        assert(body_text.find(expected_text) != std::string_view::npos);
        assert(body_text.find("[Preview truncated]") != std::string_view::npos);
    } else {
        assert(body_text == expected_text);
        assert(body_text.find("[Preview truncated]") == std::string_view::npos);
    }
    assert(lv_obj_get_height(document) > 0);
    assert(lv_label_get_long_mode(title) == LV_LABEL_LONG_WRAP);
    assert(lv_label_get_long_mode(body) == LV_LABEL_LONG_WRAP);
}

void assert_more(lv_obj_t *root)
{
    auto *sheet = require_named(root, "files_more_sheet");
    require_named(root, "files_overlay_scrim");
    require_named(root, "files_new_folder_button");
    require_named(root, "files_more_cancel");
    assert(lv_color_eq(lv_obj_get_style_bg_color(sheet, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));
}

void assert_new_folder(lv_obj_t *root)
{
    auto *dialog   = require_named(root, "files_new_folder_dialog");
    auto *input    = require_named(root, "files_folder_name_input");
    auto *create   = require_named(root, "files_folder_create");
    auto *keyboard = require_named(root, "files_folder_keyboard");
    require_named(root, "files_folder_cancel");
    require_named(root, "files_folder_hint");
    assert(lv_obj_get_width(dialog) <= 472);
    assert(lv_obj_get_width(dialog) > 0);
    assert(lv_obj_get_width(input) > 0);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(lv_color_eq(lv_obj_get_style_bg_color(create, LV_PART_MAIN), lv_color_hex(components::color_action)));
}

void assert_keyboard_geometry(lv_obj_t *root, lv_obj_t *keyboard, bool folder_editor)
{
    auto *surface                       = require_named(root, "files_surface");
    auto *page                          = require_named(root, "files_page");
    const std::int32_t available_height = lv_obj_get_content_height(surface);
    const std::int32_t minimum_height   = folder_editor ? 160 : 180;
    const std::int32_t height_percent   = folder_editor ? 36 : 42;
    const std::int32_t maximum_height   = folder_editor ? 260 : 300;
    const std::int32_t expected_height =
        std::min(maximum_height, std::max(minimum_height, available_height * height_percent / 100));
    assert(lv_obj_get_height(keyboard) == expected_height);

    lv_area_t page_area{};
    lv_area_t keyboard_area{};
    lv_obj_get_coords(page, &page_area);
    lv_obj_get_coords(keyboard, &keyboard_area);
    assert(keyboard_area.x1 >= page_area.x1);
    assert(keyboard_area.x2 <= page_area.x2);
    assert(keyboard_area.y1 >= page_area.y1);
    assert(keyboard_area.y2 == page_area.y2);

    if (folder_editor) {
        lv_area_t dialog_area{};
        lv_obj_get_coords(require_named(root, "files_new_folder_dialog"), &dialog_area);
        assert(dialog_area.y2 < keyboard_area.y1);
    }
}

void enter_home_search(lv_obj_t *root, std::string_view query)
{
    click(root, "files_search_field");
    auto *input = require_named(root, "files_search_input");
    lv_textarea_set_text(input, std::string(query).c_str());
    settle(root);
    assert_search(root, query);
}

void exercise_resize_preserves_search(lv_display_t *display, lv_obj_t *root, int width, int height)
{
    auto *page     = require_named(root, "files_page");
    auto *input    = require_named(root, "files_search_input");
    auto *scroller = require_named(root, "files_scroller");
    auto *count    = require_named(root, "files_result_count");
    auto *keyboard = require_named(root, "files_keyboard");
    assert(std::string_view(lv_label_get_text(count)) == "24 results");
    assert_keyboard_geometry(root, keyboard, false);

    lv_obj_scroll_to_y(scroller, 180, LV_ANIM_OFF);
    settle(root);
    const std::int32_t scroll_before = lv_obj_get_scroll_y(scroller);
    assert(scroll_before > 0);

    active_width  = height;
    active_height = width;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);

    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_search_input") == input);
    assert(require_named(root, "files_scroller") == scroller);
    assert(require_named(root, "files_keyboard") == keyboard);
    assert(std::string_view(lv_textarea_get_text(input)) == "report");
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(lv_obj_get_scroll_y(scroller) > 0);
    assert_keyboard_geometry(root, keyboard, false);
    assert(render_has_contrast(display, active_width, active_height));

    active_width  = width;
    active_height = height;
    lv_display_set_resolution(display, width, height);
    settle(root);
    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_search_input") == input);
    assert(require_named(root, "files_keyboard") == keyboard);
    assert(std::string_view(lv_textarea_get_text(input)) == "report");
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(lv_obj_get_scroll_y(scroller) > 0);
    assert_keyboard_geometry(root, keyboard, false);
}

void exercise_resize_preserves_folder_editor(lv_display_t *display, lv_obj_t *root, int width, int height,
                                             std::string_view expected_name)
{
    auto *page     = require_named(root, "files_page");
    auto *dialog   = require_named(root, "files_new_folder_dialog");
    auto *input    = require_named(root, "files_folder_name_input");
    auto *keyboard = require_named(root, "files_folder_keyboard");
    assert(std::string_view(lv_textarea_get_text(input)) == expected_name);
    assert_keyboard_geometry(root, keyboard, true);

    active_width  = height;
    active_height = width;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);

    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_new_folder_dialog") == dialog);
    assert(require_named(root, "files_folder_name_input") == input);
    assert(require_named(root, "files_folder_keyboard") == keyboard);
    assert(std::string_view(lv_textarea_get_text(input)) == expected_name);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert_keyboard_geometry(root, keyboard, true);
    assert(render_has_contrast(display, active_width, active_height));

    active_width  = width;
    active_height = height;
    lv_display_set_resolution(display, width, height);
    settle(root);
    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_new_folder_dialog") == dialog);
    assert(require_named(root, "files_folder_name_input") == input);
    assert(require_named(root, "files_folder_keyboard") == keyboard);
    assert(std::string_view(lv_textarea_get_text(input)) == expected_name);
    assert_keyboard_geometry(root, keyboard, true);
}

void exercise_resize_preserves_folder(lv_display_t *display, lv_obj_t *root, int width, int height,
                                      std::string_view expected_title, std::string_view expected_path,
                                      std::string_view expected_entry)
{
    auto *page     = require_named(root, "files_page");
    auto *scroller = require_named(root, "files_scroller");
    auto *title    = require_named(root, "files_nav_title");
    auto *path     = require_named(root, "files_path_label");
    auto *entry    = find_entry_row(root, expected_entry);
    assert_folder_named(root, expected_title, expected_path, true);

    active_width  = height;
    active_height = width;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);

    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_scroller") == scroller);
    assert(require_named(root, "files_nav_title") == title);
    assert(require_named(root, "files_path_label") == path);
    assert(find_entry_row(root, expected_entry) == entry);
    assert_folder_named(root, expected_title, expected_path, true);
    assert(render_has_contrast(display, active_width, active_height));

    active_width  = width;
    active_height = height;
    lv_display_set_resolution(display, width, height);
    settle(root);

    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_scroller") == scroller);
    assert(require_named(root, "files_nav_title") == title);
    assert(require_named(root, "files_path_label") == path);
    assert(find_entry_row(root, expected_entry) == entry);
    assert_folder_named(root, expected_title, expected_path, true);
}

void exercise_resize_preserves_preview(lv_display_t *display, lv_obj_t *root, int width, int height,
                                       std::string_view expected_name)
{
    assert_preview(root, expected_name, "LILYGO preview line 0", true);
    auto *page     = require_named(root, "files_page");
    auto *scroller = require_named(root, "files_scroller");
    auto *document = require_named(root, "files_preview_document");
    auto *title    = require_named(root, "files_preview_title");
    auto *body     = require_named(root, "files_preview_body");
    const std::string preview_text(lv_label_get_text(body));
    const lv_font_t *title_font = lv_obj_get_style_text_font(title, LV_PART_MAIN);
    assert(title_font != nullptr);
    assert(lv_obj_get_height(title) > title_font->line_height);
    assert(!lv_obj_has_flag(document, LV_OBJ_FLAG_SCROLLABLE));
    assert(lv_obj_get_scroll_dir(scroller) == LV_DIR_VER);
    assert(lv_obj_get_scroll_bottom(scroller) > 0);
    assert_inside(title, document);
    assert_inside(body, document);

    lv_obj_scroll_to_y(scroller, 480, LV_ANIM_OFF);
    settle(root);
    const std::int32_t scroll_before = lv_obj_get_scroll_y(scroller);
    assert(scroll_before > 0);

    active_width  = height;
    active_height = width;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);

    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_scroller") == scroller);
    assert(require_named(root, "files_preview_document") == document);
    assert(require_named(root, "files_preview_title") == title);
    assert(require_named(root, "files_preview_body") == body);
    assert(std::string_view(lv_label_get_text(title)) == expected_name);
    assert(std::string_view(lv_label_get_text(body)) == preview_text);
    assert(lv_obj_get_scroll_y(scroller) == scroll_before);
    assert(render_has_contrast(display, active_width, active_height));

    lv_obj_scroll_to_y(scroller, 0, LV_ANIM_OFF);
    settle(root);
    assert(lv_obj_get_scroll_y(scroller) == 0);
    lv_obj_scroll_to_y(scroller, scroll_before, LV_ANIM_OFF);
    settle(root);
    assert(lv_obj_get_scroll_y(scroller) == scroll_before);

    active_width  = width;
    active_height = height;
    lv_display_set_resolution(display, width, height);
    settle(root);

    assert(require_named(root, "files_page") == page);
    assert(require_named(root, "files_scroller") == scroller);
    assert(require_named(root, "files_preview_document") == document);
    assert(require_named(root, "files_preview_title") == title);
    assert(require_named(root, "files_preview_body") == body);
    assert(std::string_view(lv_label_get_text(title)) == expected_name);
    assert(std::string_view(lv_label_get_text(body)) == preview_text);
    assert(lv_obj_get_scroll_y(scroller) == scroll_before);
}

void exercise_all_reachable_pages(lv_display_t *display, lv_obj_t *root, const TemporaryDirectory &fixture, int width,
                                  int height)
{
    assert_home(root);
    assert(render_has_contrast(display, width, height));

    click(root, find_entry_row(root, "Documents"));
    assert_folder(root);
    assert(render_has_contrast(display, width, height));
    click(root, find_entry_row(root, "Nested"));
    assert_folder_named(root, "Nested", "Documents/Nested", true);
    exercise_resize_preserves_folder(display, root, width, height, "Nested", "Documents/Nested", kShortPreviewName);

    click(root, find_entry_row(root, kShortPreviewName));
    assert_preview(root, kShortPreviewName, kShortPreviewText, false);
    assert(render_has_contrast(display, width, height));
    click(root, "files_back_button");
    assert_folder_named(root, "Nested", "Documents/Nested", true);

    const std::string long_name = long_preview_name();
    click(root, find_truncated_entry_row(root, long_name, "long-preview-"));
    exercise_resize_preserves_preview(display, root, width, height, long_name);
    click(root, "files_back_button");
    assert_folder_named(root, "Nested", "Documents/Nested", true);
    click(root, "files_back_button");
    assert_folder(root);
    click(root, "files_back_button");
    assert_home(root);
    assert(std::filesystem::remove_all(fixture.path("Documents/Nested")) > 0U);

    enter_home_search(root, "vanishing");
    auto *vanishing_row = find_entry_row(root, "vanishing.txt");
    assert(std::filesystem::remove(fixture.path("vanishing.txt")));
    click(root, vanishing_row);
    auto *search_error = require_named(root, "files_search_error");
    assert(!lv_obj_has_flag(search_error, LV_OBJ_FLAG_HIDDEN));
    assert(std::string_view(lv_label_get_text(search_error)).find("Unable to read file") != std::string_view::npos);
    assert(lv_color_eq(lv_obj_get_style_text_color(search_error, LV_PART_MAIN),
                       lv_color_hex(components::color_destructive)));

    auto *input = require_named(root, "files_search_input");
    const std::string long_query(100, 'q');
    const std::string bounded_query(FilesModel::text_capacity - 1U, 'q');
    lv_textarea_set_text(input, long_query.c_str());
    settle(root);
    assert(std::string_view(lv_textarea_get_text(input)) == bounded_query);
    assert(lv_obj_has_flag(search_error, LV_OBJ_FLAG_HIDDEN));
    assert(std::string_view(lv_label_get_text(search_error)).empty());

    click(root, "files_back_button");
    assert_home(root);
    click(root, "files_search_field");
    input = require_named(root, "files_search_input");
    assert(std::string_view(lv_textarea_get_text(input)) == bounded_query);
    assert(lv_obj_find_by_name(root, "files_search_error") == nullptr);
    lv_textarea_set_text(input, "report");
    settle(root);
    assert_search(root, "report");
    assert(render_has_contrast(display, width, height));
    exercise_resize_preserves_search(display, root, width, height);

    input = require_named(root, "files_search_input");
    lv_textarea_set_text(input, "README");
    settle(root);
    click(root, find_entry_row(root, "README-large.txt"));
    assert_preview(root, "README-large.txt", "LILYGO preview line 0", true);
    assert(render_has_contrast(display, width, height));
    click(root, "files_back_button");
    assert_home(root);

    click(root, "files_more_button");
    assert_more(root);
    assert(render_has_contrast(display, width, height));
    click(root, "files_new_folder_button");
    assert_new_folder(root);
    assert(render_has_contrast(display, width, height));

    auto *folder_input = require_named(root, "files_folder_name_input");
    lv_textarea_set_text(folder_input, "../invalid");
    settle(root);
    click(root, "files_folder_create");
    assert_new_folder(root);
    folder_input      = require_named(root, "files_folder_name_input");
    auto *folder_hint = require_named(root, "files_folder_hint");
    assert(std::string_view(lv_textarea_get_text(folder_input)) == "../invalid");
    assert(std::string_view(lv_label_get_text(folder_hint)) == "Folder name is invalid");
    assert(lv_color_eq(lv_obj_get_style_text_color(folder_hint, LV_PART_MAIN),
                       lv_color_hex(components::color_destructive)));

    const std::string long_folder_name(100, 'f');
    const std::string bounded_folder_name(FilesModel::text_capacity - 1U, 'f');
    lv_textarea_set_text(folder_input, long_folder_name.c_str());
    settle(root);
    assert(std::string_view(lv_textarea_get_text(folder_input)) == bounded_folder_name);
    assert(std::string_view(lv_label_get_text(folder_hint)) == "Folder will be created in the current folder.");
    assert(lv_color_eq(lv_obj_get_style_text_color(folder_hint, LV_PART_MAIN),
                       lv_color_hex(components::color_muted_text)));

    auto *folder_keyboard = require_named(root, "files_folder_keyboard");
    lv_obj_send_event(folder_keyboard, LV_EVENT_READY, nullptr);
    settle(root);
    assert(lv_obj_has_flag(folder_keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(folder_keyboard) == nullptr);
    click(root, folder_input);
    assert(!lv_obj_has_flag(folder_keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(folder_keyboard) == folder_input);
    exercise_resize_preserves_folder_editor(display, root, width, height, bounded_folder_name);

    folder_input = require_named(root, "files_folder_name_input");
    lv_textarea_set_text(folder_input, "Created-by-render-test");
    settle(root);
    click(root, "files_folder_create");
    assert_home(root);
    assert(std::filesystem::is_directory(fixture.path("Created-by-render-test")));
    find_entry_row(root, "Created-by-render-test");
    assert(render_has_contrast(display, width, height));

    click(root, "files_more_button");
    click(root, "files_new_folder_button");
    folder_input = require_named(root, "files_folder_name_input");
    assert(std::string_view(lv_textarea_get_text(folder_input)) == FilesModel::default_folder_name);
    lv_textarea_set_text(folder_input, "cancelled-draft");
    settle(root);
    click(root, "files_folder_cancel");
    assert_home(root);
    click(root, "files_more_button");
    click(root, "files_new_folder_button");
    folder_input = require_named(root, "files_folder_name_input");
    assert(std::string_view(lv_textarea_get_text(folder_input)) == FilesModel::default_folder_name);
    click(root, "files_folder_cancel");
    assert_home(root);
}

void route_to_snapshot(lv_obj_t *root, std::string_view scenario)
{
    if (scenario == "home") return;
    if (scenario == "folder") {
        click(root, find_entry_row(root, "Documents"));
        assert_folder(root);
        return;
    }
    if (scenario == "search") {
        enter_home_search(root, "report");
        return;
    }
    if (scenario == "preview") {
        click(root, find_entry_row(root, "README-large.txt"));
        assert_preview(root, "README-large.txt", "LILYGO preview line 0", true);
        return;
    }
    if (scenario == "more") {
        click(root, "files_more_button");
        assert_more(root);
        return;
    }
    if (scenario == "new-folder") {
        click(root, "files_more_button");
        click(root, "files_new_folder_button");
        assert_new_folder(root);
        return;
    }
    assert(false && "unknown render scenario");
}

void populate_fixture(const TemporaryDirectory &fixture)
{
    fixture.make_directory("Documents/Nested");
    fixture.write_file("Documents/inside.txt", "inside Documents\n");
    fixture.write_file("Documents/Nested/short-note.txt", kShortPreviewText);
    fixture.make_directory(std::string(180, 'd'));

    std::string preview;
    preview.reserve(12000);
    for (int line = 0; line < 900; ++line) {
        preview += "LILYGO preview line " + std::to_string(line) + "\n";
    }
    assert(preview.size() > FilesModel::preview_capacity);
    fixture.write_file("README-large.txt", preview);
    fixture.write_file(std::string("Documents/Nested/") + long_preview_name(), preview);
    fixture.write_file("vanishing.txt", "removed before preview\n");

    for (int index = 0; index < 24; ++index) {
        const std::string name =
            "report-" + (index < 10 ? std::string("0") : std::string()) + std::to_string(index) + ".txt";
        fixture.write_file(name, "render fixture\n");
    }
}

struct ButtonCount {
    std::size_t value = 0;
};

lv_obj_tree_walk_res_t count_buttons_cb(lv_obj_t *object, void *user_data)
{
    auto *count = static_cast<ButtonCount *>(user_data);
    if (lv_obj_check_type(object, &lv_button_class)) ++count->value;
    return LV_OBJ_TREE_WALK_NEXT;
}

void open_app(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context, std::string_view root_path)
{
    lilygo::ui::files::set_root(root_path);
    descriptor->open(&context);
    settle(context.root);
}

void close_app(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context)
{
    descriptor->close();
    settle(context.root);
}

void exercise_boundary_matrix(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context, lv_display_t *display,
                              int width, int height, const char *output_path)
{
    {
        TemporaryDirectory fixture("render-init-error");
        const std::string missing_root = fixture.path("missing-root").string();
        open_app(descriptor, context, missing_root);
        assert_home_shell(context.root);
        auto *error = require_named(context.root, "files_error_label");
        assert(std::string_view(lv_label_get_text(error)).find("Unable to open home directory") !=
               std::string_view::npos);
        assert(
            lv_color_eq(lv_obj_get_style_text_color(error, LV_PART_MAIN), lv_color_hex(components::color_destructive)));
        assert(lv_obj_find_by_name(context.root, "files_entry_list") == nullptr);
        assert(render_has_contrast(display, width, height));
        close_app(descriptor, context);
    }

    {
        TemporaryDirectory fixture("render-empty-root");
        open_app(descriptor, context, fixture.root().string());
        assert_home_shell(context.root);
        auto *empty = require_named(context.root, "files_empty_state");
        assert(std::string_view(lv_label_get_text(empty)) == "This folder is empty");
        assert(lv_obj_find_by_name(context.root, "files_entry_list") == nullptr);

        click(context.root, "files_search_field");
        assert_search(context.root, {});
        auto *result_count = require_named(context.root, "files_result_count");
        assert(std::string_view(lv_label_get_text(result_count)) == "0 results");
        auto *search_empty = require_named(context.root, "files_search_empty");
        assert(std::string_view(lv_label_get_text(search_empty)) == "No matching items");
        assert_keyboard_geometry(context.root, require_named(context.root, "files_keyboard"), false);
        click(context.root, "files_back_button");

        click(context.root, "files_more_button");
        assert_more(context.root);
        auto *sheet = require_named(context.root, "files_more_sheet");
        assert(lv_obj_get_child_count(sheet) == 3U);
        click(context.root, "files_more_cancel");
        assert_home_shell(context.root);
        assert(lv_obj_find_by_name(context.root, "files_more_sheet") == nullptr);
        assert(lv_obj_find_by_name(context.root, "files_overlay_scrim") == nullptr);

        click(context.root, "files_more_button");
        click(context.root, "files_new_folder_button");
        assert_new_folder(context.root);
        assert_keyboard_geometry(context.root, require_named(context.root, "files_folder_keyboard"), true);
        click(context.root, "files_folder_cancel");
        assert_home_shell(context.root);
        assert(render_has_contrast(display, width, height));
        close_app(descriptor, context);
    }

    {
        TemporaryDirectory fixture("render-empty-folder");
        fixture.make_directory("Empty");
        open_app(descriptor, context, fixture.root().string());
        click(context.root, find_entry_row(context.root, "Empty"));
        assert_folder_named(context.root, "Empty", "Empty", false);
        assert(render_has_contrast(display, width, height));
        close_app(descriptor, context);
    }

    {
        TemporaryDirectory fixture("render-directory-error");
        fixture.make_directory("Gone");
        open_app(descriptor, context, fixture.root().string());
        auto *gone = find_entry_row(context.root, "Gone");
        assert(std::filesystem::remove(fixture.path("Gone")));
        click(context.root, gone);
        assert_home_shell(context.root);
        auto *error = require_named(context.root, "files_error_label");
        assert(std::string_view(lv_label_get_text(error)).find("Unable to open folder") != std::string_view::npos);
        assert(lv_obj_find_by_name(context.root, "files_entry_list") == nullptr);
        assert(render_has_contrast(display, width, height));
        close_app(descriptor, context);
    }

    {
        TemporaryDirectory fixture("render-refresh-error");
        fixture.make_directory("Parent/Child");
        open_app(descriptor, context, fixture.root().string());
        click(context.root, find_entry_row(context.root, "Parent"));
        click(context.root, find_entry_row(context.root, "Child"));
        assert_folder_named(context.root, "Child", "Parent/Child", false);

        std::filesystem::rename(fixture.path("Parent"), fixture.path("Parent-away"));
        click(context.root, "files_back_button");
        require_named(context.root, "files_navbar");
        require_named(context.root, "files_back_button");
        auto *title = require_named(context.root, "files_nav_title");
        auto *path  = require_named(context.root, "files_path_label");
        auto *error = require_named(context.root, "files_error_label");
        assert(std::string_view(lv_label_get_text(title)) == "Child");
        assert(std::string_view(lv_label_get_text(path)) == "Parent/Child");
        assert(std::string_view(lv_label_get_text(error)).find("Unable to read directory") != std::string_view::npos);
        assert(lv_obj_find_by_name(context.root, "files_entry_list") == nullptr);
        assert(lv_obj_find_by_name(context.root, "files_empty_state") == nullptr);
        assert(render_has_contrast(display, width, height));

        std::filesystem::rename(fixture.path("Parent-away"), fixture.path("Parent"));
        click(context.root, "files_back_button");
        assert_folder_named(context.root, "Parent", "Parent", true);
        close_app(descriptor, context);
    }

    {
        TemporaryDirectory fixture("render-binary-preview");
        fixture.write_file("firmware.bin", "not text\n");
        open_app(descriptor, context, fixture.root().string());
        click(context.root, find_entry_row(context.root, "firmware.bin"));
        auto *title = require_named(context.root, "files_preview_title");
        auto *body  = require_named(context.root, "files_preview_body");
        assert(std::string_view(lv_label_get_text(title)) == "firmware.bin");
        assert(std::string_view(lv_label_get_text(body)) == "Preview is not available for this file type.");
        ButtonCount buttons;
        lv_obj_tree_walk(require_named(context.root, "files_page"), count_buttons_cb, &buttons);
        assert(buttons.value == 1U);
        assert(lv_obj_find_by_name(context.root, "files_more_button") == nullptr);
        assert(render_has_contrast(display, width, height));
        close_app(descriptor, context);
    }

    {
        TemporaryDirectory fixture("render-long-path");
        const std::string first         = "first-" + std::string(72, 'a');
        const std::string second        = "second-" + std::string(72, 'b');
        const std::string third         = "third-" + std::string(72, 'c');
        const std::string relative_path = first + "/" + second + "/" + third;
        fixture.make_directory(relative_path);

        open_app(descriptor, context, fixture.root().string());
        click(context.root, find_truncated_entry_row(context.root, first, "first-"));
        click(context.root, find_truncated_entry_row(context.root, second, "second-"));
        click(context.root, find_truncated_entry_row(context.root, third, "third-"));
        require_named(context.root, "files_navbar");
        require_named(context.root, "files_back_button");
        auto *title      = require_named(context.root, "files_nav_title");
        auto *path_label = require_named(context.root, "files_path_label");
        auto *frame      = require_named(context.root, "files_frame");
        require_named(context.root, "files_empty_state");
        assert(lv_obj_find_by_name(context.root, "files_entry_list") == nullptr);
        assert_label_starts_with(title, "third-");
        assert(lv_label_get_long_mode(title) == LV_LABEL_LONG_DOT);
        assert_label_starts_with(path_label, "first-");
        assert(lv_label_get_long_mode(path_label) == LV_LABEL_LONG_DOT);
        assert_inside(title, frame);
        assert_inside(path_label, frame);
        assert(render_has_contrast(display, width, height));
        assert(write_ppm(output_path, width, height));
        close_app(descriptor, context);
    }
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 5);
    const int width  = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    const std::string_view scenario(argv[3]);
    assert(width >= 320 && height >= 240);
    assert((width == 568 && height == 1232) || (width == 1232 && height == 568) || (width == 320 && height == 568) ||
           (width == 1024 && height == 768));

    TemporaryDirectory fixture("render");
    if (scenario != "boundaries") populate_fixture(fixture);

    framebuffer_stride = std::max(width, height);
    framebuffer.resize(static_cast<std::size_t>(framebuffer_stride) * static_cast<std::size_t>(framebuffer_stride));
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(framebuffer_stride) * kPartialRows);
    active_width  = width;
    active_height = height;

    lv_init();
    auto *display = lv_display_create(width, height);
    assert(display != nullptr);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    cm0_app_context_t context{};
    context.root = lv_obj_create(lv_screen_active());
    lv_obj_set_name_static(context.root, "files_test_host");
    lv_obj_set_size(context.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(context.root, 0, 0);
    lv_obj_set_style_radius(context.root, 0, 0);
    lv_obj_set_style_pad_all(context.root, 0, 0);
    lv_obj_set_style_pad_top(context.root, 64, 0);

    const cm0_app_descriptor_t *descriptor = cm0_app_get_descriptor();
    assert(descriptor != nullptr);
    if (scenario == "boundaries") {
        exercise_boundary_matrix(descriptor, context, display, width, height, argv[4]);
    } else {
        open_app(descriptor, context, fixture.root().string());
        exercise_all_reachable_pages(display, context.root, fixture, width, height);
        assert(std::filesystem::remove(fixture.path("Created-by-render-test")));
        close_app(descriptor, context);
        open_app(descriptor, context, fixture.root().string());

        route_to_snapshot(context.root, scenario);
        assert(render_has_contrast(display, width, height));
        assert(write_ppm(argv[4], width, height));
        close_app(descriptor, context);
    }

    lilygo::ui::files::set_root({});
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
