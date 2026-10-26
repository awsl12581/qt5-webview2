#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace webview
{
namespace mime
{
inline std::string_view fromPath(std::string_view path)
{
    constexpr std::array mappings {
        std::pair { "html", "text/html" },
        std::pair { "htm", "text/html" },
        std::pair { "css", "text/css" },
        std::pair { "js", "text/javascript" },
        std::pair { "mjs", "text/javascript" },
        std::pair { "cjs", "text/javascript" },
        std::pair { "json", "application/json" },
        std::pair { "map", "application/json" },
        std::pair { "xml", "application/xml" },
        std::pair { "txt", "text/plain" },
        std::pair { "svg", "image/svg+xml" },
        std::pair { "png", "image/png" },
        std::pair { "jpg", "image/jpeg" },
        std::pair { "jpeg", "image/jpeg" },
        std::pair { "gif", "image/gif" },
        std::pair { "webp", "image/webp" },
        std::pair { "avif", "image/avif" },
        std::pair { "apng", "image/apng" },
        std::pair { "ico", "image/x-icon" },
        std::pair { "bmp", "image/bmp" },
        std::pair { "woff", "font/woff" },
        std::pair { "woff2", "font/woff2" },
        std::pair { "ttf", "font/ttf" },
        std::pair { "otf", "font/otf" },
        std::pair { "eot", "application/vnd.ms-fontobject" },
        std::pair { "wasm", "application/wasm" },
        std::pair { "mp3", "audio/mpeg" },
        std::pair { "wav", "audio/wav" },
        std::pair { "ogg", "audio/ogg" },
        std::pair { "flac", "audio/flac" },
        std::pair { "mp4", "video/mp4" },
        std::pair { "webm", "video/webm" },
        std::pair { "ogv", "video/ogg" },
        std::pair { "pdf", "application/pdf" },
        std::pair { "zip", "application/zip" },
    };

    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash) || dot + 1 >= path.size()) {
        return "application/octet-stream";
    }

    std::array<char, 16> extension {};
    const auto length = path.size() - dot - 1;
    if (length >= extension.size()) {
        return "application/octet-stream";
    }
    for (std::size_t i = 0; i < length; ++i) {
        const char character = path[dot + 1 + i];
        extension[i] = character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
    }
    const std::string_view normalized(extension.data(), length);
    for (const auto& [suffix, type] : mappings) {
        if (normalized == suffix) {
            return type;
        }
    }
    return "application/octet-stream";
}

inline std::string fromPathString(std::string_view path)
{
    return std::string(fromPath(path));
}
}
}
