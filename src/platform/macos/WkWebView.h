#pragma once

#include <system_webview/system_webview.h>

#include <memory>

namespace webview
{
class WkWebViewSession;
class WebViewState;
struct WkSessionState;

class WkWebView final : public IWebView
{
public:
    ~WkWebView() override;

    QWidget* widget() override;
    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    void attachNativeView() override;
    void detachNativeView() override;
    void open(WebApplicationPtr application, const QString& route) override;
    void navigate(const QUrl& url) override;
    void loadDocument(const QString& html, const QUrl& baseUrl) override;
    void stop() override;
    void reload() override;
    void close() override;
    bool isClosed() const override;
    WebViewBridge& bridge() override;
    WebResourceManager& resources() override;
    void setHostCallbacks(WebViewHostCallbacks callbacks) override;

    void* createPopup(void* configuration, const NewWindowRequest& request);
    std::shared_ptr<WebViewState> stateForHostCompletion() const;
    bool ownsNativeView(void* nativeView) const;

private:
    friend class WkWebViewSession;
    WkWebView(QWidget* parent, void* configuration, WebViewPolicyPtr policy, std::shared_ptr<WkSessionState> sessionState);
    void initialize(void* configuration, WebViewPolicyPtr policy, std::shared_ptr<WkSessionState> sessionState);

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
