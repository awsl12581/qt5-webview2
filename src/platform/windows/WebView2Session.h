#pragma once

#include "webview/IWebViewSession.h"
#include "webview/WebViewPolicy.h"

#include <memory>

struct ICoreWebView2Environment;

namespace webview
{
class WebView2View;
class WebView2Session final : public IWebViewSession
{
public:
    WebView2Session(WebViewSessionOptions options, WebViewPolicyPtr policy);
    ~WebView2Session() override;

    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    WebApplicationPtr createApplication(WebApplicationOptions options) override;
    WebViewPtr createWebView(QWidget* parent) override;
    void clearCache(ClearCompletion completion) override;
    void clearCookies(ClearCompletion completion) override;
    void clearWebsiteData(ClearCompletion completion) override;
    CapabilitySupport capabilitySupport(WebViewCapability capability) const override;

private:
    friend class WebView2View;
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
