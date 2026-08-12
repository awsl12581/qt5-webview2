#include "platform/macos/WkWebViewSession.h"

#include "platform/macos/WkWebView.h"

namespace webview
{
class WkWebViewSession::Impl
{
public:
    QString profilePath;
    bool ephemeral = false;
    WebViewPolicyPtr policy;
};

WkWebViewSession::WkWebViewSession(QString profilePath, bool ephemeral, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>())
{
    impl_->profilePath = std::move(profilePath);
    impl_->ephemeral = ephemeral;
    impl_->policy = policy ? std::move(policy) : createDefaultWebViewPolicy();
}

WkWebViewSession::~WkWebViewSession() = default;

WebViewPtr WkWebViewSession::createWebView(QWidget* parent)
{
    return std::make_unique<WkWebView>(parent, impl_->policy);
}

namespace {
void unsupported(const IWebViewSession::ClearCompletion& completion)
{
    if (completion) {
        completion({ false, QStringLiteral("Website data clearing is not implemented yet.") });
    }
}
}

void WkWebViewSession::clearCache(ClearCompletion completion) { unsupported(completion); }
void WkWebViewSession::clearCookies(ClearCompletion completion) { unsupported(completion); }
void WkWebViewSession::clearWebsiteData(ClearCompletion completion) { unsupported(completion); }
} // namespace webview
