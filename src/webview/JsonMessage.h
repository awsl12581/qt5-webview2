#pragma once

#include <system_webview/system_webview.h>

#include <optional>
#include <string>
#include <string_view>

namespace webview
{
// All JSON crossing a native boundary uses these strict, depth-limited helpers.
std::optional<std::string> serializeJson(const Json& value) noexcept;
std::optional<std::string> encodeBridgeMessage(const BridgeMessage& message) noexcept;
bool parseMessage(std::string_view text, Json* message, std::string* error = nullptr);
std::string jsonString(const Json& object, std::string_view key);
bool matchesJsonType(const Json& value, JsonType type);
} // namespace webview
