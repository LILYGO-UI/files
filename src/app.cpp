#include "app.hpp"

#include "app_identity.h"
#include "components/components.hpp"
#include "pages/files/files_view.hpp"
#include "pages/files/files_view_model.hpp"

#include <cstdlib>
#include <memory>
#include <string>

namespace {

struct AppState {
    std::string configured_root;
    std::unique_ptr<FilesViewModel> view_model;
    std::unique_ptr<FilesView> view;
};

AppState app_state;

void app_open(const cm0_app_context_t *context)
{
    if (!context || !context->root) return;

    if (app_state.view) app_state.view->destroy();
    app_state.view.reset();
    app_state.view_model.reset();

    const char *root = app_state.configured_root.empty() ? std::getenv("HOME") : app_state.configured_root.c_str();
    if (!root || !*root) root = "/";

    lv_obj_set_style_bg_color(context->root, lv_color_hex(components::color_page_background), 0);
    lv_obj_set_style_bg_grad_dir(context->root, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(context->root, LV_OPA_COVER, 0);

    app_state.view_model = std::make_unique<FilesViewModel>(root);
    app_state.view       = std::make_unique<FilesView>(*app_state.view_model);
    app_state.view->create(context->root);
}

void app_close()
{
    if (app_state.view) app_state.view->destroy();
    app_state.view.reset();
    app_state.view_model.reset();
}

const cm0_app_descriptor_t app_descriptor{
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_FILES_APP_ID, app_open, app_close,
};

}  // namespace

void lilygo::ui::files::set_root(std::string_view path)
{
    app_state.configured_root = path;
}

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
