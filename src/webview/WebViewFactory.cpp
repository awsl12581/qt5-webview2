#include "webview/WebViewFactory.h"

#if defined(__APPLE__)
#include "platform/macos/WkWebViewSession.h"
#endif

namespace webview
{
WebViewSessionPtr createPersistentSession(const QString& profilePath, WebViewPolicyPtr policy)
{
#if defined(__APPLE__)
    return std::make_unique<WkWebViewSession>(profilePath, false, std::move(policy));
#else
    static_assert(false, "No system WebView backend is configured for this platform.");
#endif
}

WebViewSessionPtr createEphemeralSession(WebViewPolicyPtr policy)
{
#if defined(__APPLE__)
    return std::make_unique<WkWebViewSession>(QString(), true, std::move(policy));
#else
    static_assert(false, "No system WebView backend is configured for this platform.");
#endif
}
} // namespace webview
