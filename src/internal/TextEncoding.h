#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace webview
{
// Strict, length-based conversion. Empty input succeeds; embedded NUL is preserved.
std::optional<std::u16string> utf8ToUtf16(std::string_view text) noexcept;
std::optional<std::string> utf16ToUtf8(std::u16string_view text) noexcept;

#ifdef _WIN32
// WebView2 and Win32 accept NUL-terminated text, so these adapters reject embedded NUL.
std::optional<std::wstring> utf8ToWide(std::string_view text) noexcept;
std::optional<std::string> wideToUtf8(std::wstring_view text) noexcept;
#endif
} // namespace webview
