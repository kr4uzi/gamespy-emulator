#pragma once
#include <string_view>

namespace gamespy {
    // Downloads must use relative Windows paths without parent traversal.
    constexpr bool is_relative_download_path(std::string_view path)
    {
        return !path.empty() && path.front() != '/' && path.front() != '\\'
            && path.find(':') == std::string_view::npos
            && path.find("..") == std::string_view::npos
            && path.find('\0') == std::string_view::npos;
    }
}
