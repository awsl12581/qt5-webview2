#pragma once

#include <QJsonObject>
#include <QString>

namespace webview
{
QString jsonForJavaScriptArgument(const QJsonObject& message);
bool parseMessage(const QString& text, QJsonObject* message, QString* error = nullptr);
} // namespace webview
