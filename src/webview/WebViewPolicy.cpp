#include "webview/WebViewPolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace {
QString normalizedOrigin(const QUrl& url)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        return { };
    }
    QUrl origin;
    origin.setScheme(url.scheme().toLower());
    origin.setHost(url.host().toLower());
    if (url.port() >= 0) {
        origin.setPort(url.port());
    }
    return origin.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::StripTrailingSlash);
}

bool isWithinRoot(const QString& filePath, const QString& rootPath)
{
    const auto file = QFileInfo(filePath).canonicalFilePath();
    const auto root = QDir(rootPath).canonicalPath();
    return !file.isEmpty() && !root.isEmpty() && (file == root || file.startsWith(root + QDir::separator()));
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
    return scheme == QStringLiteral("https") && config_.trustedHttpsOrigins.contains(normalizedOrigin(committedUrl));
}

bool WebViewPolicy::validateBridgeMessage(const BridgeMessage& message, QString* error) const
{
    const QJsonObject envelope {
        { QStringLiteral("version"), message.version },
        { QStringLiteral("type"), message.type },
        { QStringLiteral("payload"), message.payload },
    };
    const auto serialized = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    QString detail;
    if (serialized.size() > config_.maximumBridgeMessageBytes) {
        detail = QStringLiteral("Bridge message exceeds the configured size limit.");
    } else if (message.version != 1) {
        detail = QStringLiteral("Unsupported bridge protocol version.");
    } else if (!config_.bridgeSchemas.contains(message.type)) {
        detail = QStringLiteral("Bridge message type is not allowed.");
    } else {
        const auto& schema = config_.bridgeSchemas[message.type];
        for (const auto& key : schema.requiredPayloadKeys) {
            if (!message.payload.contains(key)) {
                detail = QStringLiteral("Bridge message payload is missing required field: %1").arg(key);
                break;
            }
        }
    }
    if (error) {
        *error = detail;
    }
    return detail.isEmpty();
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
