#pragma once

#include <cstdint>
#include <string_view>

namespace webview
{
struct ByteRange
{
    std::int64_t offset = 0;
    std::int64_t length = 0;
};

/// Parses a single RFC 9110 byte range against a known non-negative size.
/// Returns false for malformed, multi-range, overflowing, or unsatisfiable input.
bool parseByteRange(std::string_view header, std::int64_t totalSize, ByteRange* result);
} // namespace webview
