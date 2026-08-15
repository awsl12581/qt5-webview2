#include "platform/windows/WebView2Session.h"

#include "platform/windows/WebView2View.h"
#include "webview/WebViewState.h"

#include <QWidget>

namespace webview
{
class WebView2Session::Impl
{
public:
    explicit Impl(WebViewSessionOptions options, WebViewPolicyPtr policy)
        : options(std::move(options)), policy(std::move(policy)), state(std::make_shared<WebViewState>())
    {
        // TODO(webview2-session): replace this explicit scaffold failure with
        // CreateCoreWebView2EnvironmentWithOptions on a Windows UI thread.
        state->failInitialization(QStringLiteral(
            "Windows WebView2 environment is not initialized in the static scaffold."));
    }

    WebViewSessionOptions options;
    WebViewPolicyPtr policy;
    std::shared_ptr<WebViewState> state;
};

WebView2Session::WebView2Session(WebViewSessionOptions options, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(policy)))
{
}

WebView2Session::~WebView2Session() = default;

InitializationState WebView2Session::initializationState() const { return impl_->state->initializationState(); }

void WebView2Session::whenInitialized(InitializationCompletion completion)
{
    impl_->state->whenInitialized(std::move(completion));
}

WebViewPtr WebView2Session::createWebView(QWidget* parent)
{
    return std::unique_ptr<IWebView>(new WebView2View(parent, impl_->policy));
}

void WebView2Session::clearCache(ClearCompletion completion)
{
    if (completion) {
        completion({ false, QStringLiteral("WebView2 session initialization failed.") });
    }
}

void WebView2Session::clearCookies(ClearCompletion completion) { clearCache(std::move(completion)); }
void WebView2Session::clearWebsiteData(ClearCompletion completion) { clearCache(std::move(completion)); }

CapabilitySupport WebView2Session::capabilitySupport(WebViewCapability capability) const
{
    if (capability == WebViewCapability::FileSelection) {
        // TODO(webview2-file-selection): revisit when WebView2 exposes a host chooser event.
        return CapabilitySupport::Unsupported;
    }
    return CapabilitySupport::Unsupported;
}
} // namespace webview
