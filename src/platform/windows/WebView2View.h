#pragma once

#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

#include <memory>

class QWidget;

namespace webview
{
class WebView2Session;

class WebView2View final : public IWebView
{
public:
    ~WebView2View() override;

    QWidget* widget() override;
    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    void attachNativeView() override;
    void detachNativeView() override;
    void load(const QUrl& url) override;
    void setHtml(const QString& html, const QUrl& baseUrl) override;
    void stop() override;
    void reload() override;
    void close() override;
    bool isClosed() const override;
    void sendMessage(const BridgeMessage& message, MessageCompletion completion) override;
    void setHostCallbacks(WebViewHostCallbacks callbacks) override;

private:
    friend class WebView2Session;
    WebView2View(QWidget* parent, WebViewPolicyPtr policy);

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
