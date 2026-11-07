#pragma once

#include <system_webview/system_webview.h>

#include "internal/ResourceMapping.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace webview
{
class WebViewState;

/// Session-wide Android state. All members are accessed from the Java main
/// thread; the demo backend requires the host to call from that thread.
class AndroidSessionState
{
public:
    AndroidSessionState(WebViewSessionOptions options, WebViewPolicyPtr policy);
    ~AndroidSessionState();

    WebViewSessionOptions options;
    WebViewPolicyPtr policy;
    std::shared_ptr<std::vector<ResourceMapping>> resourceMappings = std::make_shared<std::vector<ResourceMapping>>();
    std::unordered_set<std::string> applicationIds;
    std::vector<WebApplicationPtr> applications;
    std::shared_ptr<WebViewState> scheduler;
    WebViewSessionHostCallbacks callbacks;
    std::vector<std::function<void()>> viewClosers;
    bool closed = false;
};

class AndroidSession final : public IWebViewSession
{
public:
    AndroidSession(WebViewSessionOptions options, WebViewPolicyPtr policy);
    ~AndroidSession() override;

    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    WebApplicationPtr createApplication(WebApplicationOptions options) override;
    WebViewPtr createWebView(WebViewHost host) override;
    void clearCache(ClearCompletion completion) override;
    void clearCookies(ClearCompletion completion) override;
    void clearWebsiteData(ClearCompletion completion) override;
    void setHostCallbacks(WebViewSessionHostCallbacks callbacks) override;
    bool supports(WebViewCapability capability) const override;

private:
    friend class AndroidView;

    void requestClear(int kind, ClearCompletion completion);

    std::shared_ptr<AndroidSessionState> state_;
};
} // namespace webview
