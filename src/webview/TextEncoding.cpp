#include "internal/TextEncoding.h"

#include <simdutf.h>

namespace webview
{
std::optional<std::u16string> utf8ToUtf16(std::string_view text) noexcept
{
    try {
        if (text.empty()) {
            return std::u16string();
        }
        std::u16string output(text.size(), u'\0');
        const auto result = simdutf::convert_utf8_to_utf16_with_errors(text.data(), text.size(), output.data());
        if (result.error != simdutf::SUCCESS) {
            return std::nullopt;
        }
        output.resize(result.count);
        return output;
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> utf16ToUtf8(std::u16string_view text) noexcept
{
    try {
        if (text.empty()) {
            return std::string();
        }
        // Each UTF-16 code unit needs at most three UTF-8 bytes.
        if (text.size() > std::string().max_size() / 3) {
            return std::nullopt;
        }
        std::string output(text.size() * 3, '\0');
        const auto result = simdutf::convert_utf16_to_utf8_with_errors(text.data(), text.size(), output.data());
        if (result.error != simdutf::SUCCESS) {
            return std::nullopt;
        }
        output.resize(result.count);
        return output;
    }
    catch (...) {
        return std::nullopt;
    }
}

#ifdef _WIN32
static_assert(sizeof(wchar_t) == sizeof(char16_t));

std::optional<std::wstring> utf8ToWide(std::string_view text) noexcept
{
    try {
        if (text.find('\0') != std::string_view::npos) {
            return std::nullopt;
        }
        const auto converted = utf8ToUtf16(text);
        if (!converted) {
            return std::nullopt;
        }
        return std::wstring(converted->begin(), converted->end());
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> wideToUtf8(std::wstring_view text) noexcept
{
    try {
        if (text.find(L'\0') != std::wstring_view::npos) {
            return std::nullopt;
        }
        const std::u16string input(text.begin(), text.end());
        return utf16ToUtf8(input);
    }
    catch (...) {
        return std::nullopt;
    }
}
#endif
} // namespace webview
