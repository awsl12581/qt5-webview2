#pragma once

#include "webview/WebViewTypes.h"

#include <QHash>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include <memory>

namespace webview
{
struct BridgeMessageSchema {
    QHash<QString, QJsonValue::Type> requiredPayloadFields;
    bool allowAdditionalPayloadFields = false;
};

struct WebViewPolicyConfig {
    QSet<QString> allowedAppHosts;
    QStringList allowedFileRoots;
    QSet<QString> trustedDevelopmentOrigins;
    QSet<QString> trustedHttpsOrigins;
    QHash<QString, BridgeMessageSchema> pageToHostSchemas;
    QHash<QString, BridgeMessageSchema> hostToPageSchemas;
    int maximumBridgeMessageBytes = 64 * 1024;
};

class WebViewPolicy
{
public:
    explicit WebViewPolicy(WebViewPolicyConfig config = { });
    virtual ~WebViewPolicy() = default;

    virtual NavigationDecision decideNavigation(const NavigationRequest& request) const;
    virtual NewWindowDecision decideNewWindow(const NewWindowRequest& request) const;
    virtual bool allowsBridge(const QUrl& committedUrl) const;
    virtual bool validatePageToHostMessage(const BridgeMessage& message, QString* error = nullptr) const;
    virtual bool validateHostToPageMessage(const BridgeMessage& message, QString* error = nullptr) const;
    virtual PermissionDecision decidePermission(const PermissionRequest& request) const;
    virtual DownloadDecision decideDownload(const DownloadRequest& request) const;

protected:
    const WebViewPolicyConfig& config() const;

private:
    WebViewPolicyConfig config_;
};

using WebViewPolicyPtr = std::shared_ptr<const WebViewPolicy>;
WebViewPolicyPtr createDefaultWebViewPolicy(WebViewPolicyConfig config = { });
} // namespace webview
