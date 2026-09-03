#include "app_config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <string>
#include <unistd.h>

namespace lilygo::ui::files::config {
namespace {

constexpr std::size_t line_capacity     = 4096;
constexpr std::string_view root_setting = "files.root";

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool contains_null(std::string_view text)
{
    return text.find('\0') != std::string_view::npos;
}

bool valid_slug(std::string_view slug)
{
    if (slug.empty() || contains_null(slug)) return false;

    for (const unsigned char character : slug) {
        if (std::isalnum(character) == 0 && character != '-' && character != '_' && character != '.') {
            return false;
        }
    }
    return true;
}

bool absolute_path(std::string_view path)
{
    return !path.empty() && path.front() == '/';
}

ReadResult configured_root_from(std::string_view path)
{
    ReadResult result = read_value(path, root_setting, value_capacity);
    if (result.status == ReadStatus::Found && !absolute_path(result.value)) {
        return {ReadStatus::Invalid, {}};
    }
    return result;
}

}  // namespace

ReadResult read_value(std::string_view path, std::string_view setting, std::size_t capacity)
{
    if (path.empty() || setting.empty() || capacity == 0U || contains_null(path) || contains_null(setting)) {
        return {ReadStatus::Invalid, {}};
    }

    std::ifstream file{std::string(path)};
    if (!file.is_open()) return {ReadStatus::NotFound, {}};

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() >= line_capacity) {
            return {ReadStatus::Invalid, {}};
        }

        std::string_view content = trim(line);
        if (content.empty() || content.front() == '#') continue;

        const std::size_t separator = content.find('=');
        if (separator == std::string_view::npos) continue;

        const std::string_view key = trim(content.substr(0, separator));
        if (key != setting) continue;

        const std::string_view configured_value = trim(content.substr(separator + 1U));
        if (configured_value.empty() || configured_value.size() >= capacity || contains_null(configured_value)) {
            return {ReadStatus::Invalid, {}};
        }
        return {ReadStatus::Found, std::string(configured_value)};
    }

    if (file.bad()) return {ReadStatus::Invalid, {}};
    return {ReadStatus::NotFound, {}};
}

ReadResult resolve_configured_root(std::string_view config_slug)
{
    if (!valid_slug(config_slug)) return {ReadStatus::Invalid, {}};

    bool invalid_configuration = false;
    const char *config_home    = std::getenv("XDG_CONFIG_HOME");
    const char *user_home      = std::getenv("HOME");
    std::string user_config_path;

    if (config_home != nullptr && config_home[0] != '\0') {
        user_config_path = config_home;
    } else if (user_home != nullptr && user_home[0] != '\0') {
        user_config_path = user_home;
        user_config_path += "/.config";
    }

    if (!user_config_path.empty()) {
        user_config_path += "/lilygo/ui/";
        user_config_path.append(config_slug);
        user_config_path += ".conf";

        ReadResult result = configured_root_from(user_config_path);
        if (result.status == ReadStatus::Found) return result;
        invalid_configuration = result.status == ReadStatus::Invalid;
    }

    std::string system_config_path = "/etc/lilygo/ui/";
    system_config_path.append(config_slug);
    system_config_path += ".conf";
    ReadResult result = configured_root_from(system_config_path);
    if (result.status == ReadStatus::Found) return result;
    invalid_configuration = invalid_configuration || result.status == ReadStatus::Invalid;

    return {invalid_configuration ? ReadStatus::Invalid : ReadStatus::NotFound, {}};
}

std::string default_root()
{
#ifdef FILES_SIMULATED_ROOT
    constexpr std::string_view simulated_root = FILES_SIMULATED_ROOT;
    if (!simulated_root.empty()) return std::string(simulated_root);
#endif

    const char *home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') return home;

    const passwd *account = getpwuid(getuid());
    if (account != nullptr && account->pw_dir != nullptr && account->pw_dir[0] != '\0') {
        return account->pw_dir;
    }
    return "/";
}

}  // namespace lilygo::ui::files::config
