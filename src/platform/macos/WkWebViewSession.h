#pragma once

#include "webview/IWebViewSession.h"
#include "webview/WebViewPolicy.h"

#include <memory>

namespace webview
{
class WkWebViewSession final : public IWebViewSession
{
public:
    WkWebViewSession(WebViewSessionOptions options, WebViewPolicyPtr policy);
    ~WkWebViewSession() override;

    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    WebViewPtr createWebView(QWidget* parent) override;
    void clearCache(ClearCompletion completion) override;
    void clearCookies(ClearCompletion completion) override;
    void clearWebsiteData(ClearCompletion completion) override;
    CapabilitySupport capabilitySupport(WebViewCapability capability) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
