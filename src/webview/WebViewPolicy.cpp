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
    const std::unordered_map<std::string, webview::BridgeMessageSchema>& schemas,
    int maximumBytes,
    std::string* error)
{
    const QJsonObject envelope {
        { QStringLiteral("version"), message.version },
        { QStringLiteral("kind"),
          message.kind == webview::BridgeMessageKind::Request    ? QStringLiteral("request")
          : message.kind == webview::BridgeMessageKind::Response ? QStringLiteral("response")
                                                                 : QStringLiteral("event") },
        { QStringLiteral("type"), QString::fromStdString(message.type) },
        { QStringLiteral("requestId"), QString::fromStdString(message.requestId) },
        { QStringLiteral("payload"), message.payload },
        { QStringLiteral("error"), QString::fromStdString(message.error) },
    };
    const auto serialized = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    std::string detail;
    const auto schema = schemas.find(message.type);
    if (serialized.size() > maximumBytes) {
        detail = "Bridge message exceeds the configured size limit.";
    }
    else if (message.version != 1) {
        detail = "Unsupported bridge protocol version.";
    }
    else if (schema == schemas.cend()) {
        detail = "Bridge message type is not allowed.";
    }
    else if (!(message.kind == webview::BridgeMessageKind::Response && !message.error.empty())) {
        for (auto field = schema->second.requiredPayloadFields.cbegin(); field != schema->second.requiredPayloadFields.cend(); ++field) {
            const auto value = message.payload.value(QString::fromStdString(field->first));
            if (value.isUndefined()) {
                detail = "Bridge message payload is missing required field: " + field->first;
                break;
            }
            if (value.type() != field->second) {
                detail = "Bridge message payload field " + field->first + " must be " + std::string(jsonTypeName(field->second)) + ".";
                break;
            }
        }
        if (detail.empty() && !schema->second.allowAdditionalPayloadFields) {
            for (auto field = message.payload.constBegin(); field != message.payload.constEnd(); ++field) {
                if (!schema->second.requiredPayloadFields.contains(field.key().toStdString())) {
                    detail = "Bridge message payload contains unexpected field: " + field.key().toStdString();
                    break;
                }
            }
        }
    }
    if (error) {
        *error = detail;
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
        const auto normalizeOrigins = [](std::unordered_set<std::string>& origins) {
            std::unordered_set<std::string> normalized;
            for (const auto& origin : origins) {
                const Url url(origin);
                if (url.isValid()) {
                    normalized.insert(url.origin().toString());
                }
            }
            origins = std::move(normalized);
        };
        normalizeOrigins(config.trustedDevelopmentOrigins);
        normalizeOrigins(config.trustedHttpsOrigins);
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
    if (!request.url.isValid() || (request.url.host().empty() && request.url.scheme() != "file")) {
        return NavigationDecision::Cancel;
    }
    const auto scheme = request.url.scheme();
    if (scheme == "https") {
        return NavigationDecision::Allow;
    }
    if (scheme == "http" && impl_->config.trustedDevelopmentOrigins.contains(webview::normalizedOrigin(request.url).toString())) {
        return NavigationDecision::Allow;
    }
    if (scheme == "app" && impl_->config.allowedAppHosts.contains(request.url.host())) {
        return NavigationDecision::Allow;
    }
    if (scheme == "file") {
        for (const auto& root : impl_->config.allowedFileRoots) {
            if (isWithinRoot(QString::fromStdString(request.url.toLocalFile()), QString::fromStdString(root))) {
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

bool WebViewPolicy::allowsBridge(const Url& committedUrl) const
{
    const auto scheme = committedUrl.scheme();
    if (scheme == "app") {
        return impl_->config.allowedAppHosts.contains(committedUrl.host());
    }
    const auto origin = webview::normalizedOrigin(committedUrl).toString();
    return (scheme == "https" && impl_->config.trustedHttpsOrigins.contains(origin))
           || (scheme == "http" && impl_->config.trustedDevelopmentOrigins.contains(origin));
}

bool WebViewPolicy::validatePageToHostMessage(const BridgeMessage& message, std::string* error) const
{
    return validateMessage(message, impl_->config.pageToHostSchemas, impl_->config.maximumBridgeMessageBytes, error);
}

bool WebViewPolicy::validateHostToPageMessage(const BridgeMessage& message, std::string* error) const
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
