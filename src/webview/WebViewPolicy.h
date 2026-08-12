#pragma once

#include "webview/WebViewTypes.h"

#include <QSet>
#include <QStringList>

#include <QHash>

#include <memory>

namespace webview
{
struct BridgeMessageSchema {
    QSet<QString> requiredPayloadKeys;
};

struct WebViewPolicyConfig {
    QSet<QString> allowedAppHosts;
    QStringList allowedFileRoots;
    QSet<QString> trustedHttpsOrigins;
    QHash<QString, BridgeMessageSchema> bridgeSchemas;
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
    virtual bool validateBridgeMessage(const BridgeMessage& message, QString* error) const;
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
