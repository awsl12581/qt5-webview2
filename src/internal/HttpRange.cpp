#include "internal/HttpRange.h"

#include <charconv>
#include <cctype>
#include <limits>

namespace webview
{
namespace
{
std::string_view trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool parseNumber(std::string_view value, std::int64_t* result)
{
    if (value.empty()) {
        return false;
    }
    const auto* first = value.data();
    const auto* last = first + value.size();
    const auto parsed = std::from_chars(first, last, *result, 10);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}
} // namespace

bool parseByteRange(std::string_view header, std::int64_t totalSize, ByteRange* result)
{
    if (!result || totalSize <= 0) {
        return false;
    }
    header = trim(header);
    constexpr std::string_view prefix = "bytes=";
    if (!header.starts_with(prefix)) {
        return false;
    }
    const auto value = header.substr(prefix.size());
    const auto dash = value.find('-');
    if (dash == std::string_view::npos || value.find('-', dash + 1) != std::string_view::npos) {
        return false;
    }
    const auto first = value.substr(0, dash);
    const auto last = value.substr(dash + 1);
    if (first.empty() && last.empty()) {
        return false;
    }

    if (first.empty()) {
        std::int64_t suffix = 0;
        if (!parseNumber(last, &suffix) || suffix <= 0) {
            return false;
        }
        result->offset = suffix >= totalSize ? 0 : totalSize - suffix;
        result->length = totalSize - result->offset;
        return true;
    }

    std::int64_t offset = 0;
    if (!parseNumber(first, &offset) || offset < 0 || offset >= totalSize) {
        return false;
    }
    std::int64_t end = totalSize - 1;
    if (!last.empty() && (!parseNumber(last, &end) || end < offset || end >= totalSize)) {
        return false;
    }
    result->offset = offset;
    result->length = end - offset + 1;
    return true;
}
} // namespace webview
