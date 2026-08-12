#include "webview/WebViewPolicy.h"

namespace webview
{
NavigationDecision WebViewPolicy::decideNavigation(const NavigationRequest&) const
{
    return NavigationDecision::Cancel;
}

NewWindowDecision WebViewPolicy::decideNewWindow(const NewWindowRequest&) const
{
    return NewWindowDecision::Cancel;
}

bool WebViewPolicy::allowsBridge(const QUrl&) const { return false; }

bool WebViewPolicy::validateBridgeMessage(const BridgeMessage&, QString* error) const
{
    if (error) {
        *error = QStringLiteral("Bridge messages are disabled by the default policy.");
    }
    return false;
}

PermissionDecision WebViewPolicy::decidePermission(const PermissionRequest&) const
{
    return PermissionDecision::Deny;
}

DownloadDecision WebViewPolicy::decideDownload(const DownloadRequest&) const
{
    return DownloadDecision::Cancel;
}

WebViewPolicyPtr createDefaultWebViewPolicy() { return std::make_shared<WebViewPolicy>(); }
} // namespace webview
