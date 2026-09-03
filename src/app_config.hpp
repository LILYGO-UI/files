#ifndef LILYGO_UI_FILES_APP_CONFIG_HPP
#define LILYGO_UI_FILES_APP_CONFIG_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace lilygo::ui::files::config {

enum class ReadStatus {
    Found,
    NotFound,
    Invalid,
};

struct ReadResult {
    ReadStatus status = ReadStatus::NotFound;
    std::string value;
};

inline constexpr std::size_t value_capacity = 4096;

[[nodiscard]] ReadResult read_value(std::string_view path, std::string_view setting,
                                    std::size_t capacity = value_capacity);

[[nodiscard]] ReadResult resolve_configured_root(std::string_view config_slug);

[[nodiscard]] std::string default_root();

}  // namespace lilygo::ui::files::config

#endif
