#include "webview/WebViewFactory.h"

#if defined(__APPLE__)
#include "platform/macos/WkWebViewSession.h"
#elif defined(_WIN32)
#include "platform/windows/WebView2Session.h"
#endif

namespace webview
{
WebViewSessionPtr createWebViewSession(WebViewSessionOptions options, WebViewPolicyPtr policy)
{
#if defined(__APPLE__)
    return std::make_unique<WkWebViewSession>(std::move(options), std::move(policy));
#elif defined(_WIN32)
    return std::make_unique<WebView2Session>(std::move(options), std::move(policy));
#else
    static_assert(false, "No system WebView backend is configured for this platform.");
#endif
}
} // namespace webview
