#pragma once

#include "webview/WebViewTypes.h"

#include <QSet>

#include <memory>

namespace webview
{
class WebViewPolicy
{
public:
    virtual ~WebViewPolicy() = default;

    virtual NavigationDecision decideNavigation(const NavigationRequest& request) const;
    virtual NewWindowDecision decideNewWindow(const NewWindowRequest& request) const;
    virtual bool allowsBridge(const QUrl& committedUrl) const;
    virtual bool validateBridgeMessage(const BridgeMessage& message, QString* error) const;
    virtual PermissionDecision decidePermission(const PermissionRequest& request) const;
    virtual DownloadDecision decideDownload(const DownloadRequest& request) const;
};

using WebViewPolicyPtr = std::shared_ptr<const WebViewPolicy>;
WebViewPolicyPtr createDefaultWebViewPolicy();
} // namespace webview
