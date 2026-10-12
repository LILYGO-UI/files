#ifndef LILYGO_UI_FILES_PAGES_HELP_HELP_VIEW_HPP
#define LILYGO_UI_FILES_PAGES_HELP_HELP_VIEW_HPP

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>

// A standalone, model-free page containing help for Files operations. The
// caller owns navigation state and supplies a callback for the back action.
class HelpView {
public:
    using BackCallback = void (*)(void *user_data);

    explicit HelpView(BackCallback back_callback = nullptr, void *user_data = nullptr) noexcept;
    ~HelpView() noexcept;

    HelpView(const HelpView &)            = delete;
    HelpView &operator=(const HelpView &) = delete;
    HelpView(HelpView &&)                 = delete;
    HelpView &operator=(HelpView &&)      = delete;

    // Creates the page below parent. Calling create again replaces the old
    // surface and preserves the caller's callback and user data.
    lv_obj_t *create(lv_obj_t *parent);
    void destroy() noexcept;
    void set_visible(bool visible) noexcept;

    void set_back_callback(BackCallback callback, void *user_data = nullptr) noexcept;
    [[nodiscard]] lv_obj_t *surface() const noexcept { return surface_; }

private:
    static void back_clicked(lv_event_t *event);
    static void key_pressed(lv_event_t *event);
    static lv_key_t remap_key(lv_indev_t *indev, lv_key_t key);
    static void reset_open_position(lv_timer_t *timer);
    static void size_changed(lv_event_t *event);

    static void make_plain(lv_obj_t *object) noexcept;
    static lv_obj_t *create_label(lv_obj_t *parent, const char *text, std::uint32_t size, std::uint32_t color);
    static lv_obj_t *create_box(lv_obj_t *parent);
    static lv_obj_t *create_circle_button(lv_obj_t *parent, const char *symbol, const char *name,
                                           lv_event_cb_t callback, void *user_data);
    static void invoke_back(HelpView *view) noexcept;

    void build();
    void apply_layout() noexcept;
    void configure_key_remap(bool enabled) noexcept;
    void register_focus_target(lv_obj_t *object) noexcept;
    void restore_focus_targets() noexcept;
    void remove_focus_targets() noexcept;
    [[nodiscard]] std::size_t focus_target_index(lv_obj_t *object) const noexcept;
    bool focus_adjacent(bool forward) noexcept;

    BackCallback back_callback_ = nullptr;
    void *user_data_            = nullptr;
    lv_obj_t *surface_             = nullptr;
    lv_obj_t *navbar_              = nullptr;
    lv_obj_t *scroller_            = nullptr;
    lv_obj_t *content_             = nullptr;
    lv_obj_t *back_button_         = nullptr;
    std::array<lv_obj_t *, 8> focus_targets_{};
    std::size_t focus_target_count_ = 0;
    lv_timer_t *open_reset_timer_ = nullptr;
};

#endif
