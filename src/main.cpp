#include "app.hpp"
#include "app_config.hpp"
#include "app_identity.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int run_application(int argc, char **argv)
{
    std::vector<char *> runtime_arguments;
    runtime_arguments.reserve(static_cast<std::size_t>(argc) + 1U);
    runtime_arguments.push_back(argv[0]);

    std::string command_line_root;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument.rfind("--root=", 0U) == 0U) {
            command_line_root.assign(argument.substr(7U));
            continue;
        }
        if (argument == "--root") {
            if (++index >= argc) {
                std::fprintf(stderr, "%s: --root requires a path\n", LILYGO_UI_FILES_APP_ID);
                return 2;
            }
            command_line_root = argv[index];
            continue;
        }
        runtime_arguments.push_back(argv[index]);
    }
    runtime_arguments.push_back(nullptr);

    std::string files_root = std::move(command_line_root);
    if (files_root.empty()) {
        const char *environment_root = std::getenv("LILYGO_UI_FILES_ROOT");
        if (environment_root != nullptr && environment_root[0] != '\0') {
            files_root = environment_root;
        } else {
            const auto configured = lilygo::ui::files::config::resolve_configured_root(LILYGO_UI_FILES_CONFIG_SLUG);
            if (configured.status == lilygo::ui::files::config::ReadStatus::Found) {
                files_root = configured.value;
            } else {
                if (configured.status == lilygo::ui::files::config::ReadStatus::Invalid) {
                    std::fprintf(stderr, "%s: ignoring invalid files.root configuration\n", LILYGO_UI_FILES_APP_ID);
                }
                files_root = lilygo::ui::files::config::default_root();
            }
        }
    }

    lilygo::ui::files::set_root(files_root);
    std::fprintf(stderr, "[%s] root=%s\n", LILYGO_UI_FILES_APP_ID, files_root.c_str());

    const int runtime_argc = static_cast<int>(runtime_arguments.size() - 1U);
    return cm0_app_run(runtime_argc, runtime_arguments.data(), cm0_app_get_descriptor());
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        return run_application(argc, argv);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s: %s\n", LILYGO_UI_FILES_APP_ID, error.what());
        return 1;
    }
}
