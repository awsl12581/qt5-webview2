#include "webview/WebViewFactory.h"

#if defined(__APPLE__)
#include "platform/macos/WkWebView.h"
#endif

namespace webview
{
WebViewPtr createWebView(QWidget* parent)
{
#if defined(__APPLE__)
    return std::make_unique<WkWebView>(parent);
#else
    static_assert(false, "No system WebView backend is configured for this platform.");
#endif
}
} // namespace webview
