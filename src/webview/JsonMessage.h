#pragma once

#include <QJsonObject>
#include <string>
#include <string_view>

namespace webview
{
std::string jsonForJavaScriptArgument(const QJsonObject& message);
bool parseMessage(std::string_view text, QJsonObject* message, std::string* error = nullptr);
} // namespace webview
