#pragma once

#include "internal/ResourceMapping.h"
#include "internal/HostView.h"
#include <system_webview/system_webview.h>

#include <functional>
#include <memory>
#include <string>

struct ICoreWebView2Environment;
struct ICoreWebView2;

namespace webview
{
class WebView2Session;
class WebViewState;

class WebView2View final : public IWebView
{
public:
    ~WebView2View() override;

    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    void attachNativeView() override;
    void detachNativeView() override;
    void open(WebApplicationPtr application, const std::string& route) override;
    void navigate(const Url& url) override;
    void loadDocument(const std::string& html, const Url& baseUrl) override;
    void stop() override;
    void reload() override;
    void close() override;
    bool isClosed() const override;
    WebViewBridge& bridge() override;
    WebResourceManager& resources() override;
    void setHostCallbacks(WebViewHostCallbacks callbacks) override;

private:
    friend class WebView2Session;
    WebView2View(
        HostView* host,
        bool ownsHost,
        std::function<ICoreWebView2Environment*()> environmentProvider,
        std::shared_ptr<WebViewState> sessionState,
        WebViewPolicyPtr policy,
        std::shared_ptr<std::vector<ResourceMapping>> resourceMappings,
        SessionMode sessionMode,
        std::function<std::string(ICoreWebView2*)> registerProfile,
        std::function<void(std::function<void()>)> registerSessionClose = {});

    class Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace webview
