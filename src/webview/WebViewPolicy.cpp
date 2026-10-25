#include "internal/ResourceMapping.h"
#include <system_webview/system_webview.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <string>
#include <string_view>

namespace
{
std::string_view jsonTypeName(QJsonValue::Type type)
{
    switch (type) {
    case QJsonValue::Null:
        return "null";
    case QJsonValue::Bool:
        return "boolean";
    case QJsonValue::Double:
        return "number";
    case QJsonValue::String:
        return "string";
    case QJsonValue::Array:
        return "array";
    case QJsonValue::Object:
        return "object";
    case QJsonValue::Undefined:
        return "undefined";
    }
    return "unknown";
}

bool validateMessage(
    const webview::BridgeMessage& message,
    const QHash<QString, webview::BridgeMessageSchema>& schemas,
    int maximumBytes,
    QString* error)
{
    const QJsonObject envelope {
        { QStringLiteral("version"), message.version },
        { QStringLiteral("kind"),
          message.kind == webview::BridgeMessageKind::Request    ? QStringLiteral("request")
          : message.kind == webview::BridgeMessageKind::Response ? QStringLiteral("response")
                                                                 : QStringLiteral("event") },
        { QStringLiteral("type"), message.type },
        { QStringLiteral("requestId"), message.requestId },
        { QStringLiteral("payload"), message.payload },
        { QStringLiteral("error"), message.error },
    };
    const auto serialized = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    std::string detail;
    const auto schema = schemas.constFind(message.type);
    if (serialized.size() > maximumBytes) {
        detail = "Bridge message exceeds the configured size limit.";
    }
    else if (message.version != 1) {
        detail = "Unsupported bridge protocol version.";
    }
    else if (schema == schemas.cend()) {
        detail = "Bridge message type is not allowed.";
    }
    else if (!(message.kind == webview::BridgeMessageKind::Response && !message.error.isEmpty())) {
        for (auto field = schema->requiredPayloadFields.cbegin(); field != schema->requiredPayloadFields.cend(); ++field) {
            const auto value = message.payload.value(field.key());
            if (value.isUndefined()) {
                detail = "Bridge message payload is missing required field: " + field.key().toStdString();
                break;
            }
            if (value.type() != field.value()) {
                detail = "Bridge message payload field " + field.key().toStdString() + " must be "
                         + std::string(jsonTypeName(field.value())) + ".";
                break;
            }
        }
        if (detail.empty() && !schema->allowAdditionalPayloadFields) {
            for (auto field = message.payload.constBegin(); field != message.payload.constEnd(); ++field) {
                if (!schema->requiredPayloadFields.contains(field.key())) {
                    detail = "Bridge message payload contains unexpected field: " + field.key().toStdString();
                    break;
                }
            }
        }
    }
    if (error) {
        *error = QString::fromStdString(detail);
    }
    return detail.empty();
}

bool isWithinRoot(const QString& filePath, const QString& rootPath)
{
    const auto file = QFileInfo(filePath).canonicalFilePath();
    const auto root = QDir(rootPath).canonicalPath();
    if (file.isEmpty() || root.isEmpty()) {
        return false;
    }
    const auto relative = QDir(root).relativeFilePath(file);
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) && !relative.startsWith(QStringLiteral("..\\"));
}
}

namespace webview
{
class WebViewPolicy::Impl
{
public:
    explicit Impl(WebViewPolicyConfig value)
        : config(std::move(value))
    {
    }

    WebViewPolicyConfig config;
};

WebViewPolicy::WebViewPolicy(WebViewPolicyConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

WebViewPolicy::WebViewPolicy(const WebViewPolicy& other)
    : impl_(std::make_unique<Impl>(*other.impl_))
{
}

WebViewPolicy& WebViewPolicy::operator=(const WebViewPolicy& other)
{
    if (this != &other) {
        impl_ = std::make_unique<Impl>(*other.impl_);
    }
    return *this;
}

WebViewPolicy::~WebViewPolicy() = default;

NavigationDecision WebViewPolicy::decideNavigation(const NavigationRequest& request) const
{
    if (!request.url.isValid() || request.url.isRelative()
        || request.url.host().isEmpty() && request.url.scheme() != QStringLiteral("file")) {
        return NavigationDecision::Cancel;
    }
    const auto scheme = request.url.scheme().toLower();
    if (scheme == QStringLiteral("https")) {
        return NavigationDecision::Allow;
    }
    if (scheme == QStringLiteral("http")
        && impl_->config.trustedDevelopmentOrigins.contains(webview::normalizedOrigin(request.url).toString())) {
        return NavigationDecision::Allow;
    }
    if (scheme == QStringLiteral("app") && impl_->config.allowedAppHosts.contains(request.url.host().toLower())) {
        return NavigationDecision::Allow;
    }
    if (scheme == QStringLiteral("file")) {
        for (const auto& root : impl_->config.allowedFileRoots) {
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
        return impl_->config.allowedAppHosts.contains(committedUrl.host().toLower());
    }
    const auto origin = webview::normalizedOrigin(committedUrl).toString();
    return (scheme == QStringLiteral("https") && impl_->config.trustedHttpsOrigins.contains(origin))
           || (scheme == QStringLiteral("http") && impl_->config.trustedDevelopmentOrigins.contains(origin));
}

bool WebViewPolicy::validatePageToHostMessage(const BridgeMessage& message, QString* error) const
{
    return validateMessage(message, impl_->config.pageToHostSchemas, impl_->config.maximumBridgeMessageBytes, error);
}

bool WebViewPolicy::validateHostToPageMessage(const BridgeMessage& message, QString* error) const
{
    return validateMessage(message, impl_->config.hostToPageSchemas, impl_->config.maximumBridgeMessageBytes, error);
}

int WebViewPolicy::maximumBridgeMessageBytes() const
{
    return impl_->config.maximumBridgeMessageBytes;
}

PermissionDecision WebViewPolicy::decidePermission(const PermissionRequest&) const
{
    return PermissionDecision::Deny;
}

DownloadDecision WebViewPolicy::decideDownload(const DownloadRequest&) const
{
    return DownloadDecision::Cancel;
}

const WebViewPolicyConfig& WebViewPolicy::config() const
{
    return impl_->config;
}

WebViewPolicyPtr createDefaultWebViewPolicy(WebViewPolicyConfig config)
{
    return std::make_shared<WebViewPolicy>(std::move(config));
}
} // namespace webview
