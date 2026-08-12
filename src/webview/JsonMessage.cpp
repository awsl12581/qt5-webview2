#include "webview/JsonMessage.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace webview
{
QString jsonForJavaScriptArgument(const QJsonObject& message)
{
    return QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

bool parseMessage(const QString& text, QJsonObject* message, QString* error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError ? QStringLiteral("Bridge message must be a JSON object.")
                                                                  : parseError.errorString();
        }
        return false;
    }
    *message = document.object();
    return true;
}
} // namespace webview
