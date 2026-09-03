#ifndef LILYGO_UI_FILES_APP_HPP
#define LILYGO_UI_FILES_APP_HPP

#include <cm0/app.h>

#include <string_view>

namespace lilygo::ui::files {

void set_root(std::string_view path);

}  // namespace lilygo::ui::files

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor();

#endif
