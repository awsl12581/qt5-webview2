#include "webview/WebViewPolicy.h"
#include "internal/ResourceMapping.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace {
QString jsonTypeName(QJsonValue::Type type)
{
    switch (type) {
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return QStringLiteral("boolean");
    case QJsonValue::Double:
        return QStringLiteral("number");
    case QJsonValue::String:
        return QStringLiteral("string");
    case QJsonValue::Array:
        return QStringLiteral("array");
    case QJsonValue::Object:
        return QStringLiteral("object");
    case QJsonValue::Undefined:
        return QStringLiteral("undefined");
    }
    return QStringLiteral("unknown");
}

bool validateMessage(const webview::BridgeMessage& message,
    const QHash<QString, webview::BridgeMessageSchema>& schemas, int maximumBytes, QString* error)
{
    const QJsonObject envelope {
        { QStringLiteral("version"), message.version },
        { QStringLiteral("type"), message.type },
        { QStringLiteral("payload"), message.payload },
    };
    const auto serialized = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    QString detail;
    const auto schema = schemas.constFind(message.type);
    if (serialized.size() > maximumBytes) {
        detail = QStringLiteral("Bridge message exceeds the configured size limit.");
    } else if (message.version != 1) {
        detail = QStringLiteral("Unsupported bridge protocol version.");
    } else if (schema == schemas.cend()) {
        detail = QStringLiteral("Bridge message type is not allowed.");
    } else {
        for (auto field = schema->requiredPayloadFields.cbegin();
             field != schema->requiredPayloadFields.cend(); ++field) {
            const auto value = message.payload.value(field.key());
            if (value.isUndefined()) {
                detail = QStringLiteral("Bridge message payload is missing required field: %1")
                             .arg(field.key());
                break;
            }
            if (value.type() != field.value()) {
                detail = QStringLiteral("Bridge message payload field %1 must be %2.")
                             .arg(field.key(), jsonTypeName(field.value()));
                break;
            }
        }
        if (detail.isEmpty() && !schema->allowAdditionalPayloadFields) {
            for (auto field = message.payload.constBegin();
                 field != message.payload.constEnd(); ++field) {
                if (!schema->requiredPayloadFields.contains(field.key())) {
                    detail = QStringLiteral("Bridge message payload contains unexpected field: %1")
                                 .arg(field.key());
                    break;
                }
            }
        }
    }
    if (error) {
        *error = detail;
    }
    return detail.isEmpty();
}

bool isWithinRoot(const QString& filePath, const QString& rootPath)
{
    const auto file = QFileInfo(filePath).canonicalFilePath();
    const auto root = QDir(rootPath).canonicalPath();
    if (file.isEmpty() || root.isEmpty()) {
        return false;
    }
    const auto relative = QDir(root).relativeFilePath(file);
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"));
}
}

namespace webview
{
WebViewPolicy::WebViewPolicy(WebViewPolicyConfig config)
    : config_(std::move(config))
{
}

NavigationDecision WebViewPolicy::decideNavigation(const NavigationRequest& request) const
{
    if (!request.url.isValid() || request.url.isRelative() || request.url.host().isEmpty() && request.url.scheme() != QStringLiteral("file")) {
        return NavigationDecision::Cancel;
    }
    const auto scheme = request.url.scheme().toLower();
    if (scheme == QStringLiteral("https")) {
        return NavigationDecision::Allow;
    }
    if (scheme == QStringLiteral("http")
        && config_.trustedDevelopmentOrigins.contains(webview::normalizedOrigin(request.url).toString())) {
        return NavigationDecision::Allow;
    }
    if (scheme == QStringLiteral("app") && config_.allowedAppHosts.contains(request.url.host().toLower())) {
        return NavigationDecision::Allow;
    }
    if (scheme == QStringLiteral("file")) {
        for (const auto& root : config_.allowedFileRoots) {
            if (isWithinRoot(request.url.toLocalFile(), root)) {
                return NavigationDecision::Allow;
            }
        }
    }
    return NavigationDecision::Cancel;
}

NewWindowDecision WebViewPolicy::decideNewWindow(const NewWindowRequest&) const
{
    return NewWindowDecision::Cancel;
}

bool WebViewPolicy::allowsBridge(const QUrl& committedUrl) const
{
    const auto scheme = committedUrl.scheme().toLower();
    if (scheme == QStringLiteral("app")) {
        return config_.allowedAppHosts.contains(committedUrl.host().toLower());
    }
    const auto origin = webview::normalizedOrigin(committedUrl).toString();
    return (scheme == QStringLiteral("https") && config_.trustedHttpsOrigins.contains(origin))
        || (scheme == QStringLiteral("http") && config_.trustedDevelopmentOrigins.contains(origin));
}

bool WebViewPolicy::validatePageToHostMessage(const BridgeMessage& message, QString* error) const
{
    return validateMessage(message, config_.pageToHostSchemas, config_.maximumBridgeMessageBytes, error);
}

bool WebViewPolicy::validateHostToPageMessage(const BridgeMessage& message, QString* error) const
{
    return validateMessage(message, config_.hostToPageSchemas, config_.maximumBridgeMessageBytes, error);
}

PermissionDecision WebViewPolicy::decidePermission(const PermissionRequest&) const
{
    return PermissionDecision::Deny;
}

DownloadDecision WebViewPolicy::decideDownload(const DownloadRequest&) const
{
    return DownloadDecision::Cancel;
}

const WebViewPolicyConfig& WebViewPolicy::config() const { return config_; }

WebViewPolicyPtr createDefaultWebViewPolicy(WebViewPolicyConfig config)
{
    return std::make_shared<WebViewPolicy>(std::move(config));
}
} // namespace webview
