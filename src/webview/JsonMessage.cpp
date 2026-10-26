#include "webview/JsonMessage.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace webview
{
namespace
{
constexpr int maximumJsonDepth = 128;

bool validJson(const Json& value, int depth)
{
    if (depth > maximumJsonDepth || value.is_discarded() || value.is_binary()) {
        return false;
    }
    if (value.is_number_float() && !std::isfinite(value.get<double>())) {
        return false;
    }
    if (value.is_structured()) {
        for (const auto& child : value) {
            if (!validJson(child, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

std::optional<std::string> serializeJson(const Json& value) noexcept
{
    try {
        if (!validJson(value, 0)) {
            return std::nullopt;
        }
        return value.dump();
    }
    catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> encodeBridgeMessage(const BridgeMessage& message) noexcept
{
    try {
        if (!message.payload.is_object() || !validJson(message.payload, 1)) {
            return std::nullopt;
        }
        const char* kind = message.kind == BridgeMessageKind::Request    ? "request"
                           : message.kind == BridgeMessageKind::Response ? "response"
                                                                         : "event";
        auto bytes = serializeJson(
            Json { { "version", message.version },
                   { "kind", kind },
                   { "type", message.type },
                   { "requestId", message.requestId },
                   { "payload", message.payload },
                   { "error", message.error } });
        return bytes && bytes->size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) ? std::move(bytes) : std::nullopt;
    }
    catch (...) {
        return std::nullopt;
    }
}

bool parseMessage(std::string_view text, Json* message, std::string* error)
{
    try {
        if (text.find('\0') != std::string_view::npos) {
            throw std::runtime_error("JSON input contains an unescaped NUL byte.");
        }
        auto parsed = Json::parse(text.begin(), text.end(), [](int depth, Json::parse_event_t, Json&) {
            if (depth > maximumJsonDepth) {
                throw std::runtime_error("JSON nesting exceeds 128 levels.");
            }
            return true;
        });
        if (!parsed.is_object()) {
            throw std::runtime_error("Bridge message must be a JSON object.");
        }
        *message = std::move(parsed);
        if (error) {
            error->clear();
        }
        return true;
    }
    catch (const std::exception& exception) {
        if (error) {
            *error = exception.what();
        }
        return false;
    }
}

std::string jsonString(const Json& object, std::string_view key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

bool matchesJsonType(const Json& value, JsonType type)
{
    switch (type) {
    case JsonType::Null:
        return value.is_null();
    case JsonType::Boolean:
        return value.is_boolean();
    case JsonType::Number:
        return value.is_number();
    case JsonType::String:
        return value.is_string();
    case JsonType::Array:
        return value.is_array();
    case JsonType::Object:
        return value.is_object();
    }
    return false;
}
} // namespace webview
