#include "app.hpp"
#include "components/components.hpp"
#include "pages/files/files_model.hpp"

#include "test_fixture.hpp"

#include <cm0/status_bar.h>
#include <cm0/typography.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
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

// A touch screen driven by the tests: where it is touched and whether a finger is down.
struct PointerState {
    lv_point_t point{};
    bool pressed = false;
};
PointerState pointer;

// A keypad driven by the test. Keeping the state in a read callback exercises
// LVGL's real pressed/released processing, including the short and long Enter
// paths, rather than sending synthetic object events.
struct KeypadState {
    std::uint32_t key       = LV_KEY_ENTER;
    lv_indev_state_t state  = LV_INDEV_STATE_RELEASED;
};
KeypadState keypad;
bool runtime_escape_requested = false;

void read_pointer(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = pointer.point;
    data->state = pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void read_keypad(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->key   = keypad.key;
    data->state = keypad.state;
}

// AppKit handles hardware Esc from the indev callback before LVGL forwards it
// to the focused object. This guard mirrors that exit condition so the test
// also proves the application's remap runs before the runtime callback.
void runtime_escape_guard(lv_event_t *event)
{
    auto *indev = static_cast<lv_indev_t *>(lv_event_get_target(event));
    if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED &&
        lv_indev_get_key(indev) == LV_KEY_ESC) {
        runtime_escape_requested = true;
    }
}

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

void keypad_tick(lv_obj_t *root, std::uint32_t milliseconds)
{
    for (std::uint32_t elapsed = 0; elapsed < milliseconds; elapsed += 10U) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
    settle(root);
}

void keypad_press(lv_obj_t *root, std::uint32_t key)
{
    keypad.key   = key;
    keypad.state = LV_INDEV_STATE_PRESSED;
    keypad_tick(root, 40);
}

void keypad_release(lv_obj_t *root, std::uint32_t key)
{
    keypad.key   = key;
    keypad.state = LV_INDEV_STATE_RELEASED;
    keypad_tick(root, 40);
}

void keypad_tap(lv_obj_t *root, std::uint32_t key)
{
    keypad_press(root, key);
    keypad_release(root, key);
}

void keypad_hold(lv_obj_t *root, std::uint32_t key, std::uint32_t milliseconds)
{
    keypad_press(root, key);
    keypad_tick(root, milliseconds);
    keypad_release(root, key);
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
    while (row != nullptr &&
           (lv_obj_get_name(row) == nullptr || std::strcmp(lv_obj_get_name(row), "files_entry_row"))) {
        row = lv_obj_get_parent(row);
    }
    assert(row != nullptr);
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

void assert_focused_border(lv_obj_t *object)
{
    assert(object != nullptr);
    assert(lv_obj_has_state(object, LV_STATE_FOCUSED));
    assert(lv_obj_get_style_border_width(object, LV_PART_MAIN) > 0);
    assert(lv_obj_get_style_border_opa(object, LV_PART_MAIN) != LV_OPA_TRANSP);
    assert(lv_color_eq(lv_obj_get_style_border_color(object, LV_PART_MAIN),
                       lv_color_hex(components::color_navigation_accent)));
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

void long_press(lv_obj_t *root, lv_obj_t *object)
{
    assert(object != nullptr);
    lv_obj_send_event(object, LV_EVENT_LONG_PRESSED, nullptr);
    settle(root);
}

// Touches the middle of an object through the input device for the given time, then lifts the finger.
void touch(lv_obj_t *root, lv_obj_t *object, std::uint32_t milliseconds)
{
    lv_obj_scroll_to_view_recursive(object, LV_ANIM_OFF);
    lv_obj_update_layout(object);
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    pointer.point   = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    pointer.pressed = true;
    for (std::uint32_t elapsed = 0; elapsed < milliseconds; elapsed += 10U) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
    pointer.pressed = false;
    for (int pass = 0; pass < 10; ++pass) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
    settle(root);
}

lv_obj_tree_walk_res_t find_text_cb(lv_obj_t *object, void *user_data)
{
    auto *needle = static_cast<std::string_view *>(user_data);
    if (lv_obj_check_type(object, &lv_label_class) &&
        std::string_view(lv_label_get_text(object)).find(*needle) != std::string_view::npos) {
        *needle = {};
        return LV_OBJ_TREE_WALK_END;
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

std::string read_file_text(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool tree_has_text(lv_obj_t *root, std::string_view text)
{
    lv_obj_tree_walk(root, find_text_cb, &text);
    return text.empty();
}

bool areas_overlap(const lv_area_t &left, const lv_area_t &right)
{
    return left.x1 <= right.x2 && right.x1 <= left.x2 && left.y1 <= right.y2 && right.y1 <= left.y2;
}

std::string_view label_text(lv_obj_t *root, const char *name)
{
    return lv_label_get_text(require_named(root, name));
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

void assert_pane_layout(lv_obj_t *root)
{
    auto *surface  = require_named(root, "files_surface");
    auto *frame    = require_named(root, "files_frame");
    auto *body     = require_named(root, "files_body");
    auto *chrome   = require_named(root, "files_chrome");
    auto *scroller = require_named(root, "files_scroller");
    auto *navbar   = require_named(root, "files_navbar");
    assert(lv_obj_get_width(frame) == std::min<std::int32_t>(lv_obj_get_content_width(surface), 1280));
    assert(lv_obj_get_parent(scroller) == body);
    assert(lv_obj_get_parent(navbar) == chrome);
    assert(lv_obj_get_index(chrome) == 0);

    // The navigation chrome is pinned above the scrolling content; only very short containers
    // (a compact phone turned to landscape with the keyboard open) let it scroll with the content.
    const bool short_container = lv_obj_get_content_height(surface) < 400;
    if (lv_obj_get_parent(chrome) == scroller) {
        assert(short_container);
        return;
    }
    lv_area_t chrome_area{};
    lv_area_t scroller_area{};
    lv_obj_get_coords(chrome, &chrome_area);
    lv_obj_get_coords(scroller, &scroller_area);
    assert(lv_obj_get_parent(chrome) == body);
    assert_inside(chrome, frame);
    assert(chrome_area.y2 < scroller_area.y1);
    assert(lv_obj_get_width(chrome) == lv_obj_get_width(scroller));
    assert(lv_obj_get_height(scroller) >= 120);

    // Navigation buttons are fixed-size circles that stay inside the bar.
    for (const char *name : {"files_back_button", "files_more_button"}) {
        lv_obj_t *button = lv_obj_find_by_name(root, name);
        if (!button) continue;
        assert(lv_obj_get_width(button) == 56 && lv_obj_get_height(button) == 56);
        assert(lv_obj_get_style_radius(button, LV_PART_MAIN) == LV_RADIUS_CIRCLE);
        assert_inside(button, navbar);
    }
}

void assert_entry_list_layout(lv_obj_t *list)
{
    // List rows span the content width, stack without overlap and keep names on one line.
    const std::uint32_t count    = lv_obj_get_child_count(list);
    std::int32_t previous_bottom = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        lv_obj_t *row = lv_obj_get_child(list, static_cast<std::int32_t>(index));
        assert(std::strcmp(lv_obj_get_name(row), "files_entry_row") == 0);
        assert(lv_obj_get_width(row) == lv_obj_get_content_width(list));
        assert(lv_obj_get_height(row) == 80);
        lv_area_t area{};
        lv_obj_get_coords(row, &area);
        assert(index == 0U || area.y1 > previous_bottom);
        previous_bottom = area.y2;

        auto *label           = require_named(row, "files_entry_name");
        const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        assert(font != nullptr);
        assert(lv_label_get_long_mode(label) == LV_LABEL_LONG_DOT);
        assert(lv_obj_get_height(label) <= lv_font_get_line_height(font));
        assert_inside(label, row);
        require_named(row, "files_entry_detail");
        if (lv_obj_find_by_name(row, "files_folder_icon") && !lv_obj_find_by_name(row, "files_select_indicator")) {
            assert(std::string_view(lv_label_get_text(lv_obj_get_child(row, 0))) == LV_SYMBOL_RIGHT);
        }
    }

    // A tap on the row's text must reach the row button rather than its layout containers.
    lv_obj_t *first = lv_obj_get_child(list, 0);
    lv_obj_scroll_to_view_recursive(first, LV_ANIM_OFF);
    lv_obj_update_layout(first);
    lv_area_t name_area{};
    lv_obj_get_coords(require_named(first, "files_entry_name"), &name_area);
    lv_point_t center{(name_area.x1 + name_area.x2) / 2, (name_area.y1 + name_area.y2) / 2};
    assert(lv_indev_search_obj(lv_obj_get_screen(first), &center) == first);
}

void assert_entry_grid(lv_obj_t *root, const char *name)
{
    auto *grid                = require_named(root, name);
    const std::uint32_t count = lv_obj_get_child_count(grid);
    assert(count > 0U);
    lv_obj_t *first = lv_obj_get_child(grid, 0);
    if (!lv_obj_check_type(first, &lv_button_class)) return;
    if (lv_obj_get_style_flex_flow(grid, LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN) {
        assert_entry_list_layout(grid);
        return;
    }
    assert(lv_obj_get_style_flex_flow(grid, LV_PART_MAIN) == LV_FLEX_FLOW_ROW_WRAP);

    // Columns follow the grid width: three in portrait, more in wider containers.
    const std::int32_t grid_width = lv_obj_get_content_width(grid);
    const std::int32_t expected   = std::clamp<std::int32_t>((grid_width + 16) / (160 + 16), 2, 8);
    lv_area_t grid_area{};
    lv_area_t first_area{};
    lv_area_t previous{};
    lv_obj_get_coords(grid, &grid_area);
    lv_obj_get_coords(first, &first_area);
    std::int32_t columns = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        lv_obj_t *cell = lv_obj_get_child(grid, static_cast<std::int32_t>(index));
        assert(std::strcmp(lv_obj_get_name(cell), "files_entry_row") == 0);
        assert(lv_obj_get_width(cell) == lv_obj_get_width(first));
        lv_area_t area{};
        lv_obj_get_coords(cell, &area);
        assert(area.x1 >= grid_area.x1 && area.x2 <= grid_area.x2);
        if (area.y1 == first_area.y1) ++columns;
        if (index > 0U && area.y1 == previous.y1) assert(area.x1 > previous.x2);
        previous = area;

        auto *label = require_named(cell, "files_entry_name");
        assert(lv_label_get_long_mode(label) == LV_LABEL_LONG_DOT);
        const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        assert(font != nullptr);
        assert(lv_obj_get_height(label) <= 2 * lv_font_get_line_height(font) + 10);
        assert_inside(label, cell);
    }
    assert(columns == std::min<std::int32_t>(expected, static_cast<std::int32_t>(count)));
}

void assert_home_shell(lv_obj_t *root)
{
    auto *surface = require_named(root, "files_surface");
    auto *title   = require_named(root, "files_nav_title");
    auto *search  = require_named(root, "files_search_field");
    require_named(root, "files_more_button");
    assert(lv_obj_find_by_name(root, "files_back_button") == nullptr);
    assert(lv_obj_find_by_name(root, "files_storage_card") == nullptr);
    assert(std::string_view(lv_label_get_text(title)) == "Files");
    assert(lv_obj_get_style_text_align(title, LV_PART_MAIN) == LV_TEXT_ALIGN_CENTER);
    assert_pane_layout(root);
    assert(
        lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(components::color_page_background)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(search, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));
}

void assert_home(lv_obj_t *root)
{
    assert_home_shell(root);
    assert_entry_grid(root, "files_entry_list");
    require_named(root, "files_entry_count");
}

void assert_icon_reaches_row(lv_obj_t *row, lv_obj_t *icon)
{
    // The icon is decoration inside the cell; a tap on it must still reach the cell button.
    // Hit testing only finds visible objects, so bring the cell on screen first.
    lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
    lv_obj_update_layout(row);
    lv_area_t area{};
    lv_obj_get_coords(icon, &area);
    lv_point_t center{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    assert(lv_indev_search_obj(lv_obj_get_screen(row), &center) == row);
}

void assert_entry_icons(lv_obj_t *root)
{
    auto *folder_row  = find_entry_row(root, "Documents");
    auto *folder_icon = require_named(folder_row, "files_folder_icon");
    auto *item_count  = require_named(folder_row, "files_entry_detail");
    assert(std::string_view(lv_label_get_text(item_count)) == "2 items");
    assert(lv_obj_find_by_name(folder_row, "files_entry_date") == nullptr);
    assert(lv_obj_find_by_name(folder_row, "files_document_icon") == nullptr);
    assert_icon_reaches_row(folder_row, folder_icon);

    auto *file_row  = find_entry_row(root, "README-large.txt");
    auto *file_icon = require_named(file_row, "files_document_icon");
    auto *type      = require_named(file_icon, "files_type_label");
    assert(std::string_view(lv_label_get_text(type)) == "TXT");
    assert(std::string_view(lv_label_get_text(require_named(file_row, "files_entry_detail"))) == "21 KB");
    assert(!std::string_view(lv_label_get_text(require_named(file_row, "files_entry_date"))).empty());
    assert(lv_obj_find_by_name(file_row, "files_folder_icon") == nullptr);
    assert_icon_reaches_row(file_row, file_icon);

    auto *pdf_type = require_named(find_entry_row(root, "datasheet.pdf"), "files_type_label");
    assert(std::string_view(lv_label_get_text(pdf_type)) == "PDF");
    assert(lv_color_eq(lv_obj_get_style_text_color(pdf_type, LV_PART_MAIN), lv_color_hex(components::color_type_pdf)));
    auto *audio_type = require_named(find_entry_row(root, "audio-sample.mp3"), "files_type_label");
    assert(std::string_view(lv_label_get_text(audio_type)) == "MP3");
    lv_obj_scroll_to_y(require_named(root, "files_scroller"), 0, LV_ANIM_OFF);
    settle(root);
}

void assert_folder_named(lv_obj_t *root, std::string_view expected_title, std::string_view expected_path,
                         bool has_entries)
{
    require_named(root, "files_navbar");
    require_named(root, "files_back_button");
    require_named(root, "files_more_button");
    auto *title = require_named(root, "files_nav_title");
    auto *path  = require_named(root, "files_path_label");
    if (has_entries) {
        assert_entry_grid(root, "files_entry_list");
    } else {
        require_named(root, "files_empty_state");
        assert(lv_obj_find_by_name(root, "files_entry_list") == nullptr);
    }
    assert(std::string_view(lv_label_get_text(title)) == expected_title);
    assert(std::string_view(lv_label_get_text(path)) == expected_path);
    assert(lv_label_get_long_mode(path) == LV_LABEL_LONG_DOT);
    assert_pane_layout(root);
    assert_inside(title, require_named(root, "files_navbar"));
    assert_inside(path, require_named(root, "files_navbar"));
}

void assert_folder(lv_obj_t *root)
{
    assert_folder_named(root, "Documents", "Documents", true);
}

void assert_search(lv_obj_t *root, std::string_view expected_query)
{
    auto *input    = require_named(root, "files_search_input");
    auto *keyboard = require_named(root, "files_keyboard");
    assert_entry_grid(root, "files_search_results");
    require_named(root, "files_result_count");
    assert_inside(input, require_named(root, "files_chrome"));
    assert(std::string_view(lv_textarea_get_text(input)) == expected_query);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert_pane_layout(root);
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
    assert_pane_layout(root);
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

void assert_more(lv_obj_t *root, const char *view_item = "files_view_icons", const char *sort_item = "files_sort_name",
                 std::string_view direction = "Ascending", bool expect_paste = false)
{
    auto *menu  = require_named(root, "files_more_menu");
    auto *scrim = require_named(root, "files_overlay_scrim");
    auto *more  = require_named(root, "files_more_button");
    assert(lv_obj_get_style_bg_opa(scrim, LV_PART_MAIN) == LV_OPA_TRANSP);
    assert(lv_obj_get_width(menu) <= 340);
    assert_inside(menu, require_named(root, "files_page"));
    assert(lv_color_eq(lv_obj_get_style_bg_color(menu, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));

    // The pull-down menu opens over the more button from the top-right corner.
    lv_area_t menu_area{};
    lv_area_t more_area{};
    lv_obj_get_coords(menu, &menu_area);
    lv_obj_get_coords(more, &more_area);
    assert(menu_area.x2 == more_area.x2);
    assert(menu_area.y1 <= more_area.y1);

    // Exactly these items, in iOS order; the current view and sort key are checked. Paste appears once
    // something has been copied.
    std::vector<const char *> items = {"files_menu_select", "files_new_folder_button"};
    if (expect_paste) items.push_back("files_menu_paste");
    items.push_back("files_help_button");
    for (const char *name : {"files_view_icons", "files_view_list", "files_sort_name", "files_sort_kind",
                             "files_sort_date", "files_sort_size"}) {
        items.push_back(name);
    }
    assert((lv_obj_find_by_name(menu, "files_menu_paste") != nullptr) == expect_paste);
    ButtonCount buttons;
    lv_obj_tree_walk(menu, count_buttons_cb, &buttons);
    assert(buttons.value == items.size());
    std::int32_t previous_index = -1;
    for (const char *name : items) {
        auto *item = require_named(menu, name);
        assert(lv_obj_get_parent(item) == menu);
        assert(lv_obj_get_index(item) > previous_index);
        previous_index    = lv_obj_get_index(item);
        const bool active = std::strcmp(name, view_item) == 0 || std::strcmp(name, sort_item) == 0;
        assert(lv_obj_has_state(item, LV_STATE_CHECKED) == active);
    }
    auto *subtitle = require_named(menu, "files_menu_subtitle");
    assert(lv_obj_get_parent(lv_obj_get_parent(subtitle)) == require_named(menu, sort_item));
    assert(std::string_view(lv_label_get_text(subtitle)) == direction);
}

void assert_help(lv_obj_t *root)
{
    auto *surface = require_named(root, "help_surface");
    auto *navbar  = require_named(root, "help_navbar");
    auto *back    = require_named(root, "help_back_button");
    auto *title   = require_named(root, "help_nav_title");
    auto *scroller = require_named(root, "help_scroller");
    auto *content  = require_named(root, "help_content");
    assert(!lv_obj_has_flag(surface, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(require_named(root, "files_surface"), LV_OBJ_FLAG_HIDDEN));
    assert(std::string_view(lv_label_get_text(title)) == "Help");
    assert(lv_obj_get_parent(back) == navbar);
    assert(lv_obj_get_width(content) <= 860);
    assert(lv_obj_get_height(scroller) > 0);
    assert(lv_obj_get_child_count(content) >= 5);
    // The content intentionally extends below the viewport; only its horizontal
    // bounds need to stay within the scroll container.
    lv_area_t content_area{};
    lv_area_t scroller_area{};
    lv_obj_get_coords(content, &content_area);
    lv_obj_get_coords(scroller, &scroller_area);
    assert(content_area.x1 >= scroller_area.x1 && content_area.x2 <= scroller_area.x2);
    assert_inside(back, navbar);
}

void assert_actions(lv_obj_t *root, std::string_view entry_name)
{
    auto *page    = require_named(root, "files_page");
    auto *menu    = require_named(root, "files_action_menu");
    auto *scrim   = require_named(root, "files_overlay_scrim");
    auto *preview = require_named(root, "files_action_preview");
    assert(lv_obj_get_style_bg_opa(scrim, LV_PART_MAIN) == LV_OPA_30);
    // A compact screen has no room for the menu beside, above or below the item, and turned to landscape
    // it is shorter than a grid cell; there the menu may cover the preview and the page clips the preview.
    const bool compact = std::min(active_width, active_height) < 400;
    assert(lv_obj_get_width(menu) <= 340);
    assert_inside(menu, page);
    if (!compact) assert_inside(preview, page);
    assert(!lv_obj_has_flag(preview, LV_OBJ_FLAG_CLICKABLE));
    assert(std::string_view(lv_label_get_text(require_named(preview, "files_entry_name"))) == entry_name);
    assert(lv_color_eq(lv_obj_get_style_bg_color(menu, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));

    // The lifted preview covers the long-pressed item exactly and the menu opens next to it.
    lv_area_t item_area{};
    lv_area_t preview_area{};
    lv_area_t menu_area{};
    lv_obj_get_coords(find_entry_row(root, entry_name), &item_area);
    lv_obj_get_coords(preview, &preview_area);
    lv_obj_get_coords(menu, &menu_area);
    assert(item_area.x1 == preview_area.x1 && item_area.y1 == preview_area.y1);
    assert(item_area.x2 == preview_area.x2 && item_area.y2 == preview_area.y2);
    if (!compact) assert(!areas_overlap(menu_area, preview_area));

    // Copy and Move side by side, the item commands, then Delete. Share, tags and favorites are not offered.
    static constexpr const char *items[] = {
        "files_action_copy",     "files_action_move",      "files_action_info",       "files_action_rename",
        "files_action_compress", "files_action_duplicate", "files_action_new_folder", "files_action_delete",
    };
    ButtonCount buttons;
    lv_obj_tree_walk(menu, count_buttons_cb, &buttons);
    assert(buttons.value == std::size(items));
    lv_area_t previous{};
    for (std::size_t index = 0; index < std::size(items); ++index) {
        lv_area_t area{};
        lv_obj_get_coords(require_named(menu, items[index]), &area);
        if (index == 1U) assert(area.y1 == previous.y1 && area.x1 > previous.x2);
        if (index > 1U) assert(area.y1 > previous.y2);
        previous = area;
    }
    for (std::string_view absent : {"Share", "Tags", "Favorite"}) assert(!tree_has_text(menu, absent));
    auto *delete_title = lv_obj_get_child(require_named(menu, "files_action_delete"), 1);
    assert(std::string_view(lv_label_get_text(delete_title)) == "Delete");
    assert(lv_color_eq(lv_obj_get_style_text_color(delete_title, LV_PART_MAIN),
                       lv_color_hex(components::color_destructive)));
}

void assert_info(lv_obj_t *root, std::string_view name, std::string_view kind)
{
    assert(label_text(root, "files_nav_title") == "Info");
    require_named(root, "files_info_close");
    assert(lv_obj_find_by_name(root, "files_more_button") == nullptr);
    auto *content = require_named(root, "files_info_content");
    auto *list    = require_named(root, "files_info_list");
    assert(lv_obj_get_width(content) <= 860);
    assert(label_text(root, "files_info_name") == name);
    assert(label_text(root, "files_info_kind") == kind);
    assert(label_text(root, "files_info_kind_value") == kind);
    assert(label_text(root, "files_info_location_value") == "Files");
    const std::string_view size = label_text(root, "files_info_size_value");
    assert(size.find(" bytes (") != std::string_view::npos && size.find(" on disk)") != std::string_view::npos);
    for (const char *value : {"files_info_kind_value", "files_info_size_value", "files_info_created_value",
                              "files_info_modified_value", "files_info_location_value"}) {
        auto *label = require_named(list, value);
        assert(!std::string_view(lv_label_get_text(label)).empty());
        assert(lv_label_get_long_mode(label) == LV_LABEL_LONG_WRAP);
        assert_inside(label, list);
    }
    auto *open = require_named(root, "files_info_open");
    assert(lv_color_eq(lv_obj_get_style_bg_color(open, LV_PART_MAIN), lv_color_hex(components::color_action)));
    assert(lv_obj_find_by_name(content, "files_folder_icon") || lv_obj_find_by_name(content, "files_document_icon"));
    assert_inside(list, content);
    assert_pane_layout(root);
}

lv_obj_t *find_move_destination(lv_obj_t *root, std::string_view name)
{
    auto *list                = require_named(root, "files_move_list");
    const std::uint32_t count = lv_obj_get_child_count(list);
    for (std::uint32_t index = 0; index < count; ++index) {
        lv_obj_t *row   = lv_obj_get_child(list, static_cast<std::int32_t>(index));
        lv_obj_t *label = lv_obj_find_by_name(row, "files_entry_name");
        if (label && std::string_view(lv_label_get_text(label)) == name) return row;
    }
    assert(false && "move destination was not found");
    return nullptr;
}

void assert_move(lv_obj_t *root, std::string_view title, bool can_move)
{
    // Narrow screens end a long title with an ellipsis.
    const std::string_view shown = label_text(root, "files_nav_title");
    const std::size_t kept = shown.size() > 3U && shown.substr(shown.size() - 3U) == "..." ? shown.size() - 3U : 0U;
    assert(shown == title || (kept > 0U && title.substr(0, kept) == shown.substr(0, kept)));
    require_named(root, "files_move_cancel");
    require_named(root, "files_move_path");
    require_named(root, "files_move_hint");
    auto *confirm = require_named(root, "files_move_confirm");
    assert(lv_obj_has_state(confirm, LV_STATE_DISABLED) == !can_move);
    const std::uint32_t background = can_move ? components::color_action : components::color_secondary_control;
    assert(lv_color_eq(lv_obj_get_style_bg_color(confirm, LV_PART_MAIN), lv_color_hex(background)));
    assert_inside(confirm, require_named(root, "files_navbar"));
    assert((lv_obj_find_by_name(root, "files_move_up") != nullptr) == (title != "Files"));

    auto *list                = require_named(root, "files_move_list");
    const std::uint32_t count = lv_obj_get_child_count(list);
    for (std::uint32_t index = 0; index < count; ++index) {
        lv_obj_t *row = lv_obj_get_child(list, static_cast<std::int32_t>(index));
        if (std::strcmp(lv_obj_get_name(row), "files_move_up") == 0) continue;
        assert(std::strcmp(lv_obj_get_name(row), "files_move_destination") == 0);
        assert(lv_obj_get_height(row) == 80);
        require_named(row, "files_folder_icon");
        require_named(row, "files_entry_name");
    }
    assert_pane_layout(root);
}

void assert_rename(lv_obj_t *root, std::string_view expected_name)
{
    auto *dialog   = require_named(root, "files_rename_dialog");
    auto *input    = require_named(root, "files_rename_input");
    auto *keyboard = require_named(root, "files_rename_keyboard");
    auto *confirm  = require_named(root, "files_rename_confirm");
    require_named(root, "files_rename_cancel");
    require_named(root, "files_rename_hint");
    assert(lv_obj_get_width(dialog) <= 472 && lv_obj_get_width(dialog) > 0);
    assert(std::string_view(lv_textarea_get_text(input)) == expected_name);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(lv_color_eq(lv_obj_get_style_bg_color(confirm, LV_PART_MAIN), lv_color_hex(components::color_action)));
    lv_area_t dialog_area{};
    lv_area_t keyboard_area{};
    lv_obj_get_coords(dialog, &dialog_area);
    lv_obj_get_coords(keyboard, &keyboard_area);
    assert(dialog_area.y2 < keyboard_area.y1);
}

void assert_delete_dialog(lv_obj_t *root, std::string_view title, std::string_view message)
{
    auto *dialog  = require_named(root, "files_delete_dialog");
    auto *confirm = require_named(root, "files_delete_confirm");
    require_named(root, "files_delete_cancel");
    assert(label_text(root, "files_delete_title") == title);
    assert(label_text(root, "files_delete_message") == message);
    assert(lv_color_eq(lv_obj_get_style_bg_color(confirm, LV_PART_MAIN), lv_color_hex(components::color_destructive)));
    assert_inside(dialog, require_named(root, "files_page"));
}

void assert_delete(lv_obj_t *root, std::string_view name)
{
    assert_delete_dialog(root, "Delete \u201c" + std::string(name) + "\u201d?",
                         "This item will be deleted immediately. You can\u2019t undo this action.");
}

std::size_t count_items(lv_obj_t *collection)
{
    return lv_obj_get_child_count(collection);
}

std::size_t count_checked_items(lv_obj_t *root)
{
    auto *collection     = require_named(root, "files_entry_list");
    std::size_t selected = 0;
    for (std::size_t index = 0; index < count_items(collection); ++index) {
        lv_obj_t *item     = lv_obj_get_child(collection, static_cast<std::int32_t>(index));
        auto *ring         = require_named(item, "files_select_indicator");
        const bool checked = lv_obj_has_state(item, LV_STATE_CHECKED);
        assert(lv_obj_has_state(ring, LV_STATE_CHECKED) == checked);
        assert(lv_obj_has_flag(lv_obj_get_child(ring, 0), LV_OBJ_FLAG_HIDDEN) == !checked);
        if (checked) ++selected;
    }
    return selected;
}

void assert_selection_toolbar(lv_obj_t *root, bool enabled)
{
    // Below the scrolling content: Duplicate, Move and Delete in a capsule and the selection menu's more
    // button on the right, all disabled and muted until something is selected.
    auto *page     = require_named(root, "files_page");
    auto *toolbar  = require_named(root, "files_select_toolbar");
    auto *capsule  = require_named(toolbar, "files_select_actions");
    auto *more     = require_named(toolbar, "files_select_more");
    auto *scroller = require_named(root, "files_scroller");
    assert(lv_obj_get_parent(toolbar) == require_named(root, "files_body"));
    assert_inside(toolbar, page);
    lv_area_t toolbar_area{};
    lv_area_t scroller_area{};
    lv_area_t page_area{};
    lv_obj_get_coords(toolbar, &toolbar_area);
    lv_obj_get_coords(scroller, &scroller_area);
    lv_obj_get_coords(page, &page_area);
    assert(scroller_area.y2 < toolbar_area.y1);
    assert(toolbar_area.y2 == page_area.y2);
    assert(lv_color_eq(lv_obj_get_style_bg_color(capsule, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));

    const std::uint32_t icon_color = enabled ? components::color_container_title : components::color_muted_text;
    const auto assert_button       = [&](lv_obj_t *button) {
        assert(lv_obj_get_height(button) == 56);
        assert_inside(button, toolbar);
        assert(lv_obj_has_state(button, LV_STATE_DISABLED) == !enabled);
        assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(button, 0), LV_PART_MAIN),
                                 lv_color_hex(icon_color)));
    };
    ButtonCount buttons;
    lv_obj_tree_walk(toolbar, count_buttons_cb, &buttons);
    assert(buttons.value == 4U);
    lv_area_t previous{};
    std::int32_t index = 0;
    for (const char *name : {"files_select_duplicate", "files_select_move", "files_select_delete"}) {
        auto *button = require_named(capsule, name);
        assert(lv_obj_get_parent(button) == capsule && lv_obj_get_index(button) == index++);
        assert_button(button);
        lv_area_t area{};
        lv_obj_get_coords(button, &area);
        assert(index == 1 || area.x1 > previous.x2);
        previous = area;
    }
    assert_button(more);
    assert(lv_obj_get_width(more) == 56);
    lv_area_t capsule_area{};
    lv_area_t more_area{};
    lv_obj_get_coords(capsule, &capsule_area);
    lv_obj_get_coords(more, &more_area);
    assert(capsule_area.x2 < more_area.x1);
}

void assert_select_mode(lv_obj_t *root, std::size_t expected_selected)
{
    auto *navbar     = require_named(root, "files_navbar");
    auto *title      = require_named(root, "files_nav_title");
    auto *select_all = require_named(root, "files_select_all_button");
    auto *done       = require_named(root, "files_select_done");
    assert(lv_obj_find_by_name(root, "files_more_button") == nullptr);
    assert(lv_obj_find_by_name(root, "files_back_button") == nullptr);
    assert(lv_color_eq(lv_obj_get_style_bg_color(done, LV_PART_MAIN), lv_color_hex(components::color_action)));
    assert_inside(select_all, navbar);
    assert_inside(done, navbar);
    assert_pane_layout(root);

    const std::size_t total = count_items(require_named(root, "files_entry_list"));
    assert(count_checked_items(root) == expected_selected);
    const std::string expected_title =
        expected_selected == 0U ? std::string("Select Items") : std::to_string(expected_selected) + " Selected";
    assert(std::string_view(lv_label_get_text(title)) == expected_title);
    const std::string_view select_all_text(lv_label_get_text(lv_obj_get_child(select_all, 0)));
    assert(select_all_text == (expected_selected == total ? "Deselect All" : "Select All"));
    assert_selection_toolbar(root, expected_selected > 0U);
}

void assert_selection_menu(lv_obj_t *root, std::size_t selected)
{
    auto *page  = require_named(root, "files_page");
    auto *menu  = require_named(root, "files_selection_menu");
    auto *scrim = require_named(root, "files_overlay_scrim");
    auto *more  = require_named(root, "files_select_more");
    assert(lv_obj_get_style_bg_opa(scrim, LV_PART_MAIN) == LV_OPA_TRANSP);
    assert(lv_obj_get_width(menu) <= 340);
    assert_inside(menu, page);
    assert(lv_color_eq(lv_obj_get_style_bg_color(menu, LV_PART_MAIN),
                       lv_color_hex(components::color_container_background)));

    // The menu opens above the toolbar's more button and shares its right edge.
    lv_area_t menu_area{};
    lv_area_t more_area{};
    lv_obj_get_coords(menu, &menu_area);
    lv_obj_get_coords(more, &more_area);
    assert(menu_area.x2 == more_area.x2);
    assert(menu_area.y2 < more_area.y1);

    // Copy, New Folder with the items and Compress, in iOS order; tags and share are not offered.
    const std::string items = std::to_string(selected) + (selected == 1U ? " Item" : " Items");
    const std::pair<const char *, std::string> rows[] = {
        {"files_selection_copy", "Copy " + items},
        {"files_selection_new_folder", "New Folder with " + items},
        {"files_selection_compress", "Compress"},
    };
    ButtonCount buttons;
    lv_obj_tree_walk(menu, count_buttons_cb, &buttons);
    assert(buttons.value == std::size(rows));
    std::int32_t previous_bottom = menu_area.y1;
    for (const auto &[name, title] : rows) {
        auto *row = require_named(menu, name);
        lv_area_t area{};
        lv_obj_get_coords(row, &area);
        assert(area.y1 >= previous_bottom);
        previous_bottom = area.y2;
        assert(lv_obj_get_height(row) >= 48);
        assert(std::string_view(lv_label_get_text(lv_obj_get_child(row, 1))) == title);
    }
    for (std::string_view absent : {"Share", "Tags"}) assert(!tree_has_text(menu, absent));

    // Selection mode stays beneath the menu with the same items selected.
    assert(count_checked_items(root) == selected);
    assert(label_text(root, "files_nav_title") == std::to_string(selected) + " Selected");
}

std::string first_file_name(lv_obj_t *root)
{
    auto *collection = require_named(root, "files_entry_list");
    for (std::size_t index = 0; index < count_items(collection); ++index) {
        lv_obj_t *item = lv_obj_get_child(collection, static_cast<std::int32_t>(index));
        if (!lv_obj_find_by_name(item, "files_folder_icon")) {
            return lv_label_get_text(require_named(item, "files_entry_name"));
        }
    }
    assert(false && "no file in the collection");
    return {};
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
    auto *chrome   = require_named(root, "files_chrome");
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
    assert(require_named(root, "files_chrome") == chrome);
    assert(std::string_view(lv_textarea_get_text(input)) == "report");
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert_pane_layout(root);
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
    assert(require_named(root, "files_chrome") == chrome);
    assert(std::string_view(lv_textarea_get_text(input)) == "report");
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert(lv_obj_get_scroll_y(scroller) > 0);
    assert_pane_layout(root);
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

void exercise_menu_and_selection(lv_display_t *display, lv_obj_t *root, int width, int height)
{
    // The list view shows "date - size" rows; search results follow the same view.
    click(root, "files_more_button");
    assert_more(root);
    assert(render_has_contrast(display, width, height));
    click(root, "files_view_list");
    assert(lv_obj_find_by_name(root, "files_more_menu") == nullptr);
    assert_entry_grid(root, "files_entry_list");
    const std::string_view summary(
        lv_label_get_text(require_named(find_entry_row(root, "README-large.txt"), "files_entry_detail")));
    assert(summary.size() > 8U && summary.substr(summary.size() - 8U) == " - 21 KB");
    assert(render_has_contrast(display, width, height));
    enter_home_search(root, "report");
    click(root, "files_back_button");
    assert_home_shell(root);

    // Size puts the largest file first; choosing Size again reverses the order.
    click(root, "files_more_button");
    click(root, "files_sort_size");
    assert(first_file_name(root) == "README-large.txt");
    click(root, "files_more_button");
    assert_more(root, "files_view_list", "files_sort_size", "Descending");
    click(root, "files_sort_size");
    assert(first_file_name(root) == "datasheet.pdf");
    click(root, "files_more_button");
    assert_more(root, "files_view_list", "files_sort_size", "Ascending");
    click(root, "files_sort_name");
    assert(first_file_name(root) == "audio-sample.mp3");
    click(root, "files_more_button");
    click(root, "files_view_icons");
    assert_home(root);

    // Selection mode keeps the scroll position, toggles items in place and never opens them.
    auto *scroller = require_named(root, "files_scroller");
    lv_obj_scroll_to_y(scroller, 200, LV_ANIM_OFF);
    settle(root);
    const std::int32_t scroll_before = lv_obj_get_scroll_y(scroller);
    assert(scroll_before > 0);
    click(root, "files_more_button");
    assert(lv_obj_get_scroll_y(require_named(root, "files_scroller")) == scroll_before);
    click(root, "files_menu_select");
    assert_select_mode(root, 0U);
    auto *page = require_named(root, "files_page");
    scroller   = require_named(root, "files_scroller");
    assert(lv_obj_get_scroll_y(scroller) == scroll_before);

    // A long press while selecting neither opens the context menu nor changes the selection.
    long_press(root, find_entry_row(root, "Documents"));
    assert(lv_obj_find_by_name(root, "files_action_menu") == nullptr);
    assert(require_named(root, "files_page") == page);
    assert_select_mode(root, 0U);

    auto *folder_cell = find_entry_row(root, "Documents");
    click(root, folder_cell);
    assert(require_named(root, "files_page") == page);
    assert(lv_obj_has_state(folder_cell, LV_STATE_CHECKED));
    assert_select_mode(root, 1U);
    click(root, find_entry_row(root, "README-large.txt"));
    assert_select_mode(root, 2U);
    assert(lv_obj_get_scroll_y(scroller) == scroll_before);
    assert(render_has_contrast(display, width, height));

    const std::size_t total = count_items(require_named(root, "files_entry_list"));
    click(root, "files_select_all_button");
    assert_select_mode(root, total);
    click(root, "files_select_all_button");
    assert_select_mode(root, 0U);
    assert(require_named(root, "files_page") == page);

    click(root, folder_cell);
    click(root, "files_select_done");
    assert_home(root);
    assert(lv_obj_find_by_name(root, "files_select_indicator") == nullptr);
    auto *home_scroller = require_named(root, "files_scroller");
    assert(lv_obj_get_scroll_y(home_scroller) == 0);
    assert_inside(require_named(root, "files_search_field"), home_scroller);
}

void exercise_item_actions(lv_display_t *display, lv_obj_t *root, const TemporaryDirectory &fixture, int width,
                           int height)
{
    // Holding a finger on an item opens its context menu, and lifting it does not open the item.
    touch(root, find_entry_row(root, "Documents"), 600);
    assert_actions(root, "Documents");
    assert(render_has_contrast(display, width, height));
    click(root, "files_overlay_scrim");
    assert_home(root);
    assert(lv_obj_find_by_name(root, "files_action_menu") == nullptr);
    touch(root, find_entry_row(root, "Documents"), 50);
    assert_folder_named(root, "Documents", "Documents", true);
    click(root, "files_back_button");
    assert_home(root);

    // The menu stays next to its item when the display turns.
    long_press(root, find_entry_row(root, "README-large.txt"));
    auto *menu = require_named(root, "files_action_menu");
    assert_actions(root, "README-large.txt");
    active_width  = height;
    active_height = width;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);
    assert(require_named(root, "files_action_menu") == menu);
    assert_actions(root, "README-large.txt");
    assert(render_has_contrast(display, active_width, active_height));
    active_width  = width;
    active_height = height;
    lv_display_set_resolution(display, width, height);
    settle(root);
    assert(require_named(root, "files_action_menu") == menu);
    assert_actions(root, "README-large.txt");
    click(root, "files_overlay_scrim");
    assert_home(root);

    // Get Info describes the item and can open it.
    long_press(root, find_entry_row(root, "Documents"));
    click(root, "files_action_info");
    assert_info(root, "Documents", "Folder");
    assert(label_text(root, "files_info_size_value").find(", 1 item") != std::string_view::npos);
    assert(render_has_contrast(display, width, height));
    click(root, "files_info_open");
    assert_folder_named(root, "Documents", "Documents", true);
    click(root, "files_back_button");
    assert_home(root);
    long_press(root, find_entry_row(root, "README-large.txt"));
    click(root, "files_action_info");
    assert_info(root, "README-large.txt", "Plain Text");
    click(root, "files_info_close");
    assert_home(root);

    // Duplicate, then rename the copy; a taken name keeps the dialog and the draft.
    long_press(root, find_entry_row(root, "report-00.txt"));
    click(root, "files_action_duplicate");
    assert_home(root);
    long_press(root, find_entry_row(root, "report-00 2.txt"));
    click(root, "files_action_rename");
    assert_rename(root, "report-00 2.txt");
    assert(render_has_contrast(display, width, height));
    lv_textarea_set_text(require_named(root, "files_rename_input"), "Documents");
    settle(root);
    click(root, "files_rename_confirm");
    assert_rename(root, "Documents");
    auto *hint = require_named(root, "files_rename_hint");
    assert(std::string_view(lv_label_get_text(hint)).find("already taken") != std::string_view::npos);
    assert(lv_color_eq(lv_obj_get_style_text_color(hint, LV_PART_MAIN), lv_color_hex(components::color_destructive)));
    lv_textarea_set_text(require_named(root, "files_rename_input"), "renamed.txt");
    settle(root);
    assert(std::string_view(lv_label_get_text(hint)) == "Enter a new name for this item.");
    click(root, "files_rename_confirm");
    assert_home(root);
    find_entry_row(root, "renamed.txt");
    assert(std::filesystem::exists(fixture.path("renamed.txt")));
    assert(!std::filesystem::exists(fixture.path("report-00 2.txt")));

    // Copy, then Paste from the more menu adds a numbered copy.
    long_press(root, find_entry_row(root, "renamed.txt"));
    click(root, "files_action_copy");
    assert_home(root);
    click(root, "files_more_button");
    assert_more(root, "files_view_icons", "files_sort_name", "Ascending", true);
    click(root, "files_menu_paste");
    assert_home(root);
    find_entry_row(root, "renamed 2.txt");

    long_press(root, find_entry_row(root, "renamed.txt"));
    click(root, "files_action_compress");
    assert_home(root);
    find_entry_row(root, "renamed.txt.zip");

    // Move browses folders; the item's own folder is not a destination.
    long_press(root, find_entry_row(root, "renamed 2.txt"));
    click(root, "files_action_move");
    assert_move(root, "Files", false);
    assert(render_has_contrast(display, width, height));
    click(root, find_move_destination(root, "Documents"));
    assert_move(root, "Documents", true);
    click(root, "files_move_up");
    assert_move(root, "Files", false);
    click(root, find_move_destination(root, "Documents"));
    click(root, "files_move_confirm");
    assert_home(root);
    assert(std::filesystem::exists(fixture.path("Documents/renamed 2.txt")));
    assert(std::filesystem::remove(fixture.path("Documents/renamed 2.txt")));

    // New Folder with 1 Item names the new folder right away.
    long_press(root, find_entry_row(root, "renamed.txt"));
    click(root, "files_action_new_folder");
    assert_rename(root, "New Folder");
    lv_textarea_set_text(require_named(root, "files_rename_input"), "Bundle");
    settle(root);
    click(root, "files_rename_confirm");
    assert_home(root);
    assert(std::filesystem::exists(fixture.path("Bundle/renamed.txt")));

    // Delete asks first; Cancel keeps the item.
    long_press(root, find_entry_row(root, "Bundle"));
    click(root, "files_action_delete");
    assert_delete(root, "Bundle");
    assert(render_has_contrast(display, width, height));
    click(root, "files_delete_cancel");
    assert_home(root);
    assert(std::filesystem::exists(fixture.path("Bundle")));
    for (const char *name : {"Bundle", "renamed.txt.zip"}) {
        long_press(root, find_entry_row(root, name));
        click(root, "files_action_delete");
        click(root, "files_delete_confirm");
        assert_home(root);
        assert(!std::filesystem::exists(fixture.path(name)));
    }

    // The copied item is gone: Paste explains why in the menu, which then no longer offers it.
    click(root, "files_more_button");
    click(root, "files_menu_paste");
    auto *error = require_named(require_named(root, "files_more_menu"), "files_menu_error");
    assert(std::string_view(lv_label_get_text(error)).find("Unable to paste") != std::string_view::npos);
    assert(lv_obj_find_by_name(root, "files_menu_paste") == nullptr);
    assert_entry_grid(root, "files_entry_list");
    click(root, "files_overlay_scrim");
    assert_home(root);
}

void enter_selection(lv_obj_t *root, std::initializer_list<std::string_view> names)
{
    click(root, "files_more_button");
    click(root, "files_menu_select");
    for (std::string_view name : names) click(root, find_entry_row(root, name));
    assert_select_mode(root, names.size());
}

void exercise_selection_actions(lv_display_t *display, lv_obj_t *root, const TemporaryDirectory &fixture, int width,
                                int height)
{
    click(root, find_entry_row(root, "Projects"));
    assert_folder_named(root, "Projects", "Projects", true);

    // Until something is selected the toolbar is disabled, and a real tap on it does nothing.
    enter_selection(root, {});
    touch(root, require_named(root, "files_select_delete"), 50);
    assert(lv_obj_find_by_name(root, "files_delete_dialog") == nullptr);
    assert_select_mode(root, 0U);
    click(root, "files_select_done");

    // A failed command keeps selection mode and explains why above the content until the selection changes.
    enter_selection(root, {"delta.txt"});
    assert(std::filesystem::remove(fixture.path("Projects/delta.txt")));
    click(root, "files_select_duplicate");
    assert_select_mode(root, 1U);
    auto *error = require_named(root, "files_select_error");
    assert(std::string_view(lv_label_get_text(error)).find("Unable to duplicate") != std::string_view::npos);
    assert(lv_color_eq(lv_obj_get_style_text_color(error, LV_PART_MAIN), lv_color_hex(components::color_destructive)));
    assert(render_has_contrast(display, width, height));
    click(root, find_entry_row(root, "delta.txt"));
    assert_select_mode(root, 0U);
    assert(lv_obj_has_flag(error, LV_OBJ_FLAG_HIDDEN));
    click(root, "files_select_done");

    // Duplicate copies every selected item next to it and leaves selection mode.
    enter_selection(root, {"alpha.txt", "beta.txt"});
    assert(render_has_contrast(display, width, height));
    click(root, "files_select_duplicate");
    assert_folder_named(root, "Projects", "Projects", true);
    assert(lv_obj_find_by_name(root, "files_select_toolbar") == nullptr);
    assert(read_file_text(fixture.path("Projects/alpha 2.txt")) == "alpha project\n");
    assert(read_file_text(fixture.path("Projects/beta 2.txt")) == "beta project\n");

    // The selection menu closes on a tap outside and keeps the selection; Compress puts several items in
    // Archive.zip.
    enter_selection(root, {"alpha 2.txt", "beta 2.txt"});
    click(root, "files_select_more");
    assert_selection_menu(root, 2U);
    assert(render_has_contrast(display, width, height));
    click(root, "files_overlay_scrim");
    assert(lv_obj_find_by_name(root, "files_selection_menu") == nullptr);
    assert_select_mode(root, 2U);
    click(root, "files_select_more");
    click(root, "files_selection_compress");
    assert_folder_named(root, "Projects", "Projects", true);
    find_entry_row(root, "Archive.zip");

    // New Folder with the items moves them into a new folder and names it right away.
    enter_selection(root, {"alpha 2.txt", "beta 2.txt", "Archive.zip"});
    click(root, "files_select_more");
    assert_selection_menu(root, 3U);
    click(root, "files_selection_new_folder");
    assert_rename(root, "New Folder");
    lv_textarea_set_text(require_named(root, "files_rename_input"), "Copies");
    settle(root);
    click(root, "files_rename_confirm");
    assert_folder_named(root, "Projects", "Projects", true);
    for (const char *name : {"alpha 2.txt", "beta 2.txt", "Archive.zip"}) {
        assert(std::filesystem::exists(fixture.path("Projects/Copies") / name));
    }

    // Move: a selected folder is no destination, and closing the sheet returns to the same selection.
    enter_selection(root, {"Copies", "gamma.md"});
    click(root, "files_select_move");
    assert_move(root, "Projects", false);
    assert(label_text(root, "files_move_hint") == "Choose a folder for 2 items.");
    assert(lv_obj_has_state(find_move_destination(root, "Copies"), LV_STATE_DISABLED));
    assert(render_has_contrast(display, width, height));
    click(root, "files_move_cancel");
    assert_select_mode(root, 2U);
    click(root, "files_select_move");
    click(root, find_move_destination(root, "Drafts"));
    assert_move(root, "Drafts", true);
    click(root, "files_move_confirm");
    assert_folder_named(root, "Projects", "Projects", true);
    assert(std::filesystem::exists(fixture.path("Projects/Drafts/Copies/Archive.zip")));
    assert(std::filesystem::exists(fixture.path("Projects/Drafts/gamma.md")));

    // Copy puts every selected item on the clipboard; Paste adds them all to another folder.
    enter_selection(root, {"alpha.txt", "beta.txt"});
    click(root, "files_select_more");
    click(root, "files_selection_copy");
    assert_folder_named(root, "Projects", "Projects", true);
    click(root, find_entry_row(root, "Drafts"));
    click(root, "files_more_button");
    assert_more(root, "files_view_icons", "files_sort_name", "Ascending", true);
    click(root, "files_menu_paste");
    assert_folder_named(root, "Drafts", "Projects/Drafts", true);
    find_entry_row(root, "alpha.txt");
    find_entry_row(root, "beta.txt");
    click(root, "files_back_button");

    // Delete asks about every selected item; Cancel keeps the selection beneath the prompt.
    enter_selection(root, {"alpha.txt", "beta.txt"});
    click(root, "files_select_delete");
    assert_delete_dialog(root, "Delete 2 Items?",
                         "These items will be deleted immediately. You can\u2019t undo this action.");
    assert(count_checked_items(root) == 2U);
    assert(render_has_contrast(display, width, height));
    click(root, "files_delete_cancel");
    assert_select_mode(root, 2U);
    click(root, "files_select_delete");
    click(root, "files_delete_confirm");
    assert_folder_named(root, "Projects", "Projects", true);
    assert(!std::filesystem::exists(fixture.path("Projects/alpha.txt")));
    assert(!std::filesystem::exists(fixture.path("Projects/beta.txt")));
    click(root, "files_back_button");
    assert_home(root);
}

void exercise_all_reachable_pages(lv_display_t *display, lv_obj_t *root, const TemporaryDirectory &fixture, int width,
                                  int height)
{
    assert_home(root);
    assert_entry_icons(root);
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

    auto *files_scroller = require_named(root, "files_scroller");
    lv_obj_scroll_to_y(files_scroller, 80, LV_ANIM_OFF);
    settle(root);
    const std::int32_t help_return_scroll = lv_obj_get_scroll_y(files_scroller);
    touch(root, require_named(root, "files_more_button"), 100);
    assert_more(root);
    assert(render_has_contrast(display, width, height));
    touch(root, require_named(root, "files_help_button"), 100);
    assert_help(root);
    assert(render_has_contrast(display, width, height));
    auto *help_scroller = require_named(root, "help_scroller");
    assert(lv_obj_get_scroll_y(help_scroller) == 0);
    if (lv_obj_get_scroll_bottom(help_scroller) > 0) {
        lv_obj_scroll_to_y(help_scroller, 200, LV_ANIM_OFF);
        settle(root);
        assert(lv_obj_get_scroll_y(help_scroller) > 0);
    }
    click(root, "help_back_button");
    assert_home(root);
    assert(lv_obj_get_scroll_y(require_named(root, "files_scroller")) == help_return_scroll);
    touch(root, require_named(root, "files_more_button"), 100);
    touch(root, require_named(root, "files_help_button"), 100);
    assert_help(root);
    assert(lv_obj_get_scroll_y(require_named(root, "help_scroller")) == 0);
    click(root, "help_back_button");
    assert_home(root);
    click(root, "files_more_button");
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

    exercise_item_actions(display, root, fixture, width, height);
    exercise_menu_and_selection(display, root, width, height);
    exercise_selection_actions(display, root, fixture, width, height);
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
    if (scenario == "help") {
        click(root, "files_more_button");
        click(root, "files_help_button");
        assert_help(root);
        return;
    }
    if (scenario == "new-folder") {
        click(root, "files_more_button");
        click(root, "files_new_folder_button");
        assert_new_folder(root);
        return;
    }
    if (scenario == "list") {
        click(root, "files_more_button");
        click(root, "files_view_list");
        assert_home(root);
        return;
    }
    if (scenario == "actions") {
        long_press(root, find_entry_row(root, "Documents"));
        assert_actions(root, "Documents");
        return;
    }
    if (scenario == "info") {
        long_press(root, find_entry_row(root, "Documents"));
        click(root, "files_action_info");
        assert_info(root, "Documents", "Folder");
        return;
    }
    if (scenario == "move") {
        long_press(root, find_entry_row(root, "README-large.txt"));
        click(root, "files_action_move");
        click(root, find_move_destination(root, "Documents"));
        assert_move(root, "Documents", true);
        return;
    }
    if (scenario == "rename") {
        long_press(root, find_entry_row(root, "README-large.txt"));
        click(root, "files_action_rename");
        assert_rename(root, "README-large.txt");
        return;
    }
    if (scenario == "delete") {
        long_press(root, find_entry_row(root, "Documents"));
        click(root, "files_action_delete");
        assert_delete(root, "Documents");
        return;
    }
    if (scenario == "select") {
        click(root, "files_more_button");
        click(root, "files_menu_select");
        click(root, find_entry_row(root, "Documents"));
        click(root, find_entry_row(root, "README-large.txt"));
        assert_select_mode(root, 2U);
        return;
    }
    if (scenario == "select-menu") {
        click(root, "files_more_button");
        click(root, "files_menu_select");
        click(root, find_entry_row(root, "Documents"));
        click(root, find_entry_row(root, "README-large.txt"));
        click(root, "files_select_more");
        assert_selection_menu(root, 2U);
        return;
    }
    if (scenario == "select-move") {
        enter_selection(root, {"Documents", "README-large.txt"});
        click(root, "files_select_move");
        assert_move(root, "Files", false);
        assert(lv_obj_has_state(find_move_destination(root, "Documents"), LV_STATE_DISABLED));
        return;
    }
    if (scenario == "select-delete") {
        enter_selection(root, {"Documents", "README-large.txt"});
        click(root, "files_select_delete");
        assert_delete_dialog(root, "Delete 2 Items?",
                             "These items will be deleted immediately. You can’t undo this action.");
        return;
    }
    assert(false && "unknown render scenario");
}

void populate_fixture(const TemporaryDirectory &fixture)
{
    fixture.make_directory("Documents/Nested");
    fixture.write_file("audio-sample.mp3", std::string(2048, 'a'));
    fixture.write_file("datasheet.pdf", "%PDF-1.4\n");
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
    fixture.write_file("Projects/alpha.txt", "alpha project\n");
    fixture.write_file("Projects/beta.txt", "beta project\n");
    fixture.write_file("Projects/delta.txt", "removed before duplicating\n");
    fixture.write_file("Projects/gamma.md", "gamma project\n");
    fixture.make_directory("Projects/Drafts");

    for (int index = 0; index < 24; ++index) {
        const std::string name =
            "report-" + (index < 10 ? std::string("0") : std::string()) + std::to_string(index) + ".txt";
        fixture.write_file(name, "render fixture\n");
    }
}

void open_app(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context, std::string_view root_path);
void close_app(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context);

void exercise_keyboard(const cm0_app_descriptor_t *descriptor, cm0_app_context_t &context, lv_display_t *display,
                       lv_group_t *group, const TemporaryDirectory &fixture, int width, int height,
                       const char *output_path)
{
    const bool portrait = height > width;
    open_app(descriptor, context, fixture.root().string());
    auto *root = context.root;
    assert_home(root);

    // Navigation controls are part of the hardware keyboard focus order and
    // expose the same visible focus treatment as entry rows.
    auto *more_button = require_named(root, "files_more_button");
    lv_group_focus_obj(more_button);
    settle(root);
    assert_focused_border(more_button);

    // ESC dismisses the pull-down menu while leaving the Files app open.
    click(root, "files_more_button");
    assert_more(root);
    runtime_escape_requested = false;
    keypad_tap(root, LV_KEY_ESC);
    assert(!runtime_escape_requested);
    assert_home(root);
    more_button = require_named(root, "files_more_button");
    lv_group_focus_obj(more_button);
    settle(root);
    assert_focused_border(more_button);

    // Help starts on its close button. Vertical arrows move focus through the
    // guidance cards and pan the viewport when focus reaches an edge.
    lv_group_focus_obj(more_button);
    keypad_tap(root, LV_KEY_ENTER);
    assert_more(root);
    auto *help_menu_button = require_named(root, "files_help_button");
    lv_group_focus_obj(help_menu_button);
    keypad_tap(root, LV_KEY_ENTER);
    assert_help(root);
    auto *help_back = require_named(root, "help_back_button");
    auto *help_scroller = require_named(root, "help_scroller");
    auto *help_content = require_named(root, "help_content");
    assert(lv_obj_get_scroll_y(help_scroller) == 0);
    lv_group_focus_obj(help_back);
    assert(lv_group_get_focused(group) == help_back);
    const std::int32_t help_scroll_bottom = lv_obj_get_scroll_bottom(help_scroller);
    const std::uint32_t help_target_count = lv_obj_get_child_count(help_content);
    assert(help_target_count >= 5);
    // Arrow keys move focus through the close button and help cards. Once the
    // last card is focused, another arrow still pans the viewport at the edge.
    for (std::uint32_t index = 0; index < help_target_count; ++index) {
        keypad_tap(root, LV_KEY_DOWN);
        assert(lv_group_get_focused(group) ==
               (index == 0 ? lv_obj_get_child(help_content, 0)
                           : lv_obj_get_child(help_content, static_cast<std::int32_t>(index))));
    }
    if (help_scroll_bottom > 0) {
        keypad_tap(root, LV_KEY_DOWN);
        assert(lv_obj_get_scroll_y(help_scroller) > 0);
        keypad_tap(root, LV_KEY_HOME);
        assert(lv_obj_get_scroll_y(help_scroller) == 0);
        assert(lv_group_get_focused(group) == lv_obj_get_child(help_content, 0));
    }

    // Tab traverses the close button and each help card. Every focused card
    // carries the scroll-on-focus flag, so the long page follows the focus.
    lv_group_focus_obj(help_back);
    for (std::uint32_t index = 0; index < help_target_count; ++index) {
        keypad_tap(root, LV_KEY_NEXT);
        assert(lv_group_get_focused(group) == lv_obj_get_child(help_content, static_cast<std::int32_t>(index)));
    }
    if (help_scroll_bottom > 0) assert(lv_obj_get_scroll_y(help_scroller) > 0);
    keypad_tap(root, LV_KEY_PREV);
    assert(lv_group_get_focused(group) == lv_obj_get_child(help_content, static_cast<std::int32_t>(help_target_count - 2)));
    runtime_escape_requested = false;
    keypad_tap(root, LV_KEY_ESC);
    assert(!runtime_escape_requested);
    assert_home(root);
    more_button = require_named(root, "files_more_button");

    const std::string long_folder_name(180, 'd');
    auto *long_name = find_truncated_entry_row(root, long_folder_name, "ddd");
    auto *documents = find_entry_row(root, "Documents");
    auto *projects  = find_entry_row(root, "Projects");
    auto *last_entry = lv_obj_get_child(require_named(root, "files_entry_list"), -1);
    assert(last_entry != nullptr);
    keypad_tap(root, LV_KEY_LEFT);
    assert(lv_group_get_focused(group) == more_button);
    assert_focused_border(more_button);
    lv_group_focus_obj(last_entry);
    // Arrow keys stay within the directory entry collection. Tab is the
    // control-navigation key that reaches the fixed More button.
    keypad_tap(root, LV_KEY_DOWN);
    assert(lv_group_get_focused(group) == last_entry);
    keypad_tap(root, LV_KEY_NEXT);
    assert(lv_group_get_focused(group) == more_button);
    assert_focused_border(more_button);
    auto *entries = require_named(root, "files_entry_list");
    std::int32_t first_row_y = INT32_MAX;
    std::int32_t rightmost_x  = INT32_MIN;
    lv_obj_t *top_right       = nullptr;
    const std::uint32_t entry_count = lv_obj_get_child_count(entries);
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        auto *entry = lv_obj_get_child(entries, static_cast<std::int32_t>(index));
        if (!entry) continue;
        lv_area_t area{};
        lv_obj_get_coords(entry, &area);
        first_row_y = std::min(first_row_y, area.y1);
    }
    for (std::uint32_t index = 0; index < entry_count; ++index) {
        auto *entry = lv_obj_get_child(entries, static_cast<std::int32_t>(index));
        if (!entry) continue;
        lv_area_t area{};
        lv_obj_get_coords(entry, &area);
        if (area.y1 == first_row_y && area.x2 > rightmost_x) {
            rightmost_x = area.x2;
            top_right   = entry;
        }
    }
    assert(top_right != nullptr);
    lv_group_focus_obj(top_right);
    keypad_tap(root, LV_KEY_RIGHT);
    auto *right_edge_target = lv_group_get_focused(group);
    assert(right_edge_target != nullptr && right_edge_target != more_button);
    const char *right_edge_name = lv_obj_get_name(right_edge_target);
    assert(right_edge_name != nullptr && std::strcmp(right_edge_name, "files_entry_row") == 0);
    assert_focused_border(right_edge_target);
    lv_group_focus_obj(top_right);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == top_right);
    keypad_tap(root, LV_KEY_LEFT);
    auto *left_entry = lv_group_get_focused(group);
    assert(left_entry != nullptr);
    const char *left_entry_name = lv_obj_get_name(left_entry);
    assert(left_entry_name != nullptr && std::strcmp(left_entry_name, "files_entry_row") == 0);
    lv_group_focus_obj(last_entry);
    keypad_tap(root, LV_KEY_NEXT);
    auto *home_search = require_named(root, "files_search_field");
    assert(lv_group_get_focused(group) == more_button);
    keypad_tap(root, LV_KEY_NEXT);
    assert(lv_group_get_focused(group) == home_search);
    assert(lv_obj_find_by_name(root, "files_keyboard") == nullptr);
    // Tab moves onto the search entry. Enter activates it.
    keypad_tap(root, LV_KEY_ENTER);
    auto *navigation_search_input = require_named(root, "files_search_input");
    assert(lv_group_get_focused(group) == navigation_search_input);
    assert(!lv_obj_has_flag(require_named(root, "files_keyboard"), LV_OBJ_FLAG_HIDDEN));
    click(root, "files_back_button");
    assert_home(root);
    more_button = require_named(root, "files_more_button");
    lv_group_focus_obj(more_button);
    keypad_tap(root, LV_KEY_NEXT);
    home_search = require_named(root, "files_search_field");
    assert(lv_group_get_focused(group) == home_search);
    keypad_tap(root, LV_KEY_PREV);
    assert(lv_group_get_focused(group) == more_button);
    long_name = find_truncated_entry_row(root, long_folder_name, "ddd");
    documents = find_entry_row(root, "Documents");
    projects  = find_entry_row(root, "Projects");
    lv_group_focus_obj(long_name);
    settle(root);
    assert_focused_border(long_name);
    assert(lv_obj_get_height(long_name) > lv_obj_get_height(documents));
    assert(lv_group_get_focused(group) == long_name);

    // The icon grid follows its visual rows and columns. The long-name cell has
    // a different height, so this also catches navigation based on child index.
    keypad_tap(root, LV_KEY_RIGHT);
    assert(lv_group_get_focused(group) == documents);
    assert_focused_border(documents);
    keypad_tap(root, LV_KEY_LEFT);
    assert(lv_group_get_focused(group) == long_name);

    keypad_tap(root, LV_KEY_DOWN);
    auto *first_below = find_entry_row(root, portrait ? "audio-sample.mp3" : "report-00.txt");
    assert(lv_group_get_focused(group) == first_below);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == long_name);

    keypad_tap(root, LV_KEY_RIGHT);
    assert(lv_group_get_focused(group) == documents);
    keypad_tap(root, LV_KEY_DOWN);
    auto *second_below = find_entry_row(root, portrait ? "datasheet.pdf" : "report-01.txt");
    assert(lv_group_get_focused(group) == second_below);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == documents);

    keypad_tap(root, LV_KEY_RIGHT);
    assert(lv_group_get_focused(group) == projects);
    keypad_tap(root, LV_KEY_DOWN);
    auto *third_below = find_entry_row(root, portrait ? "README-large.txt" : "report-02.txt");
    assert(lv_group_get_focused(group) == third_below);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == projects);

    // In list view the same directions walk the rows; right falls back to the
    // next document-order row because all rows share one x coordinate.
    click(root, "files_more_button");
    click(root, "files_view_list");
    documents = find_entry_row(root, "Documents");
    projects  = find_entry_row(root, "Projects");
    long_name = find_truncated_entry_row(root, long_folder_name, "ddd");
    assert(lv_group_get_focused(group) == long_name);
    assert_focused_border(long_name);
    keypad_tap(root, LV_KEY_RIGHT);
    assert(lv_group_get_focused(group) == documents);
    assert_focused_border(documents);
    keypad_tap(root, LV_KEY_DOWN);
    assert(lv_group_get_focused(group) == projects);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == documents);

    // Restore icon view before testing Enter activation and search navigation.
    click(root, "files_more_button");
    click(root, "files_view_icons");
    documents = find_entry_row(root, "Documents");
    long_name = find_truncated_entry_row(root, long_folder_name, "ddd");
    assert(lv_group_get_focused(group) == long_name);

    // A short Enter opens a folder. Arrow keys stay within its entries;
    // Shift-Tab reaches the Back button and Enter returns to the parent.
    lv_group_focus_obj(documents);
    keypad_tap(root, LV_KEY_ENTER);
    assert_folder_named(root, "Documents", "Documents", true);
    auto *folder_first_entry = lv_obj_get_child(require_named(root, "files_entry_list"), 0);
    assert(folder_first_entry != nullptr);
    lv_group_focus_obj(folder_first_entry);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == folder_first_entry);
    keypad_tap(root, LV_KEY_PREV);
    auto *folder_back = require_named(root, "files_back_button");
    assert(lv_group_get_focused(group) == folder_back);
    keypad_tap(root, LV_KEY_ENTER);
    assert_home(root);
    documents = find_entry_row(root, "Documents");

    // The same short Enter opens a file preview and does not show an operation menu.
    auto *audio = find_entry_row(root, "audio-sample.mp3");
    lv_group_focus_obj(audio);
    keypad_tap(root, LV_KEY_ENTER);
    assert(label_text(root, "files_preview_title") == "audio-sample.mp3");
    assert(label_text(root, "files_preview_body") == "Preview is not available for this file type.");
    assert(lv_obj_find_by_name(root, "files_action_menu") == nullptr);
    click(root, "files_back_button");
    assert_home(root);
    documents = find_entry_row(root, "Documents");
    lv_group_focus_obj(documents);

    // Holding Enter opens the same context menu as a touch long press. The
    // release after the menu appears must not activate the folder as well.
    keypad_hold(root, LV_KEY_ENTER, 850);
    assert_actions(root, "Documents");
    assert(lv_obj_find_by_name(root, "files_nav_title") == nullptr ||
           label_text(root, "files_nav_title") != "Documents");
    // ESC dismisses the item menu without terminating the application.
    runtime_escape_requested = false;
    keypad_tap(root, LV_KEY_ESC);
    assert(!runtime_escape_requested);
    assert_home(root);

    // Arrow keys stay on directory items at the top edge. Tab reaches the
    // search entry, and Enter activates Search for the next character.
    documents = find_entry_row(root, "Documents");
    lv_group_focus_obj(documents);
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == documents);
    more_button = require_named(root, "files_more_button");
    lv_group_focus_obj(more_button);
    keypad_tap(root, LV_KEY_NEXT);
    auto *search_field = require_named(root, "files_search_field");
    assert(lv_group_get_focused(group) == search_field);
    assert(lv_obj_find_by_name(root, "files_search_input") == nullptr);
    keypad_tap(root, LV_KEY_ENTER);
    search_field = require_named(root, "files_search_field");
    auto *input    = require_named(root, "files_search_input");
    auto *keyboard = require_named(root, "files_keyboard");
    assert(lv_group_get_focused(group) == input);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    assert_focused_border(search_field);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_keyboard_get_textarea(keyboard) == input);

    keypad_press(root, static_cast<std::uint32_t>('r'));
    keypad_release(root, static_cast<std::uint32_t>('r'));
    keypad_press(root, static_cast<std::uint32_t>('e'));
    keypad_release(root, static_cast<std::uint32_t>('e'));
    keypad_press(root, static_cast<std::uint32_t>('p'));
    keypad_release(root, static_cast<std::uint32_t>('p'));
    assert(std::string_view(lv_textarea_get_text(input)) == "rep");
    assert(label_text(root, "files_result_count") == "24 results");
    keypad_tap(root, LV_KEY_BACKSPACE);
    assert(std::string_view(lv_textarea_get_text(input)) == "re");
    assert(label_text(root, "files_result_count") == "25 results");

    auto *search_results = require_named(root, "files_search_results");
    auto *first_result   = lv_obj_get_child(search_results, 0);
    assert(first_result != nullptr);
    // Arrow keys do not move from the editable control to a result; Tab does.
    keypad_tap(root, LV_KEY_DOWN);
    assert(lv_group_get_focused(group) == input);
    keypad_tap(root, LV_KEY_NEXT);
    assert(lv_group_get_focused(group) == first_result);
    assert_focused_border(first_result);
    assert(!lv_obj_has_state(search_field, LV_STATE_FOCUSED));
    keypad_tap(root, LV_KEY_UP);
    assert(lv_group_get_focused(group) == first_result);
    keypad_tap(root, LV_KEY_PREV);
    assert(lv_group_get_focused(group) == input);
    assert_focused_border(search_field);

    // Resizing keeps the same focused textarea, keyboard and query.
    const int old_width = active_width;
    const int old_height = active_height;
    active_width  = old_height;
    active_height = old_width;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);
    assert(require_named(root, "files_search_input") == input);
    assert(require_named(root, "files_keyboard") == keyboard);
    assert(lv_group_get_focused(group) == input);
    assert(std::string_view(lv_textarea_get_text(input)) == "re");
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert_keyboard_geometry(root, keyboard, false);

    active_width  = old_width;
    active_height = old_height;
    lv_display_set_resolution(display, active_width, active_height);
    settle(root);
    assert(require_named(root, "files_search_input") == input);
    assert(lv_group_get_focused(group) == input);
    assert(render_has_contrast(display, active_width, active_height));
    assert(write_ppm(output_path, width, height));
    keypad_tap(root, LV_KEY_ESC);
    assert_home(root);
    close_app(descriptor, context);
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
        click(context.root, "files_overlay_scrim");
        assert_home_shell(context.root);
        assert(lv_obj_find_by_name(context.root, "files_more_menu") == nullptr);
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
    lv_indev_t *touch_screen = lv_indev_create();
    assert(touch_screen != nullptr);
    lv_indev_set_type(touch_screen, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_screen, read_pointer);
    lv_indev_set_display(touch_screen, display);

    lv_group_t *keyboard_group = nullptr;
    lv_indev_t *keypad_device  = nullptr;
    if (scenario == "keyboard") {
        // Match the AppKit runtime: create the default focus group before the
        // application builds its buttons and attach a real keypad indev to it.
        keyboard_group = lv_group_create();
        assert(keyboard_group != nullptr);
        lv_group_set_default(keyboard_group);
        keypad.key   = LV_KEY_ENTER;
        keypad.state = LV_INDEV_STATE_RELEASED;
        keypad_device = lv_indev_create();
        assert(keypad_device != nullptr);
        lv_indev_set_type(keypad_device, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(keypad_device, read_keypad);
        lv_indev_set_display(keypad_device, display);
        lv_indev_set_group(keypad_device, keyboard_group);
        lv_indev_add_event_cb(keypad_device, runtime_escape_guard, LV_EVENT_KEY, nullptr);
    }

    // Recreate the AppKit chrome used by the production runtime so exported
    // preview images include the clock, connectivity, battery, and home
    // indicator surrounding the application content.
    lv_obj_t *status_bar = cm0_status_bar_create(lv_layer_top());
    assert(status_bar != nullptr);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *home_indicator = cm0_home_indicator_create(lv_layer_top());
    assert(home_indicator != nullptr);
    lv_obj_set_size(home_indicator, LV_PCT(100), 38);
    lv_obj_align(home_indicator, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_clear_flag(home_indicator,
                      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

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
    } else if (scenario == "keyboard") {
        exercise_keyboard(descriptor, context, display, keyboard_group, fixture, width, height, argv[4]);
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
    if (keypad_device) lv_indev_delete(keypad_device);
    if (keyboard_group) {
        lv_group_set_default(nullptr);
        lv_group_delete(keyboard_group);
    }
    lv_indev_delete(touch_screen);
    lv_obj_delete(status_bar);
    lv_obj_delete(home_indicator);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
