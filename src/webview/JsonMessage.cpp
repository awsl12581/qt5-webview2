#include "webview/JsonMessage.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace webview
{
std::string jsonForJavaScriptArgument(const QJsonObject& message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString();
}

bool parseMessage(std::string_view text, QJsonObject* message, std::string* error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<int>(text.size())), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError ? "Bridge message must be a JSON object."
                                                                  : parseError.errorString().toStdString();
        }
        return false;
    }
    *message = document.object();
    return true;
}
} // namespace webview
