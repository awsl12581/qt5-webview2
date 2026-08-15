#pragma once

#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

#include <functional>
#include <memory>

struct ICoreWebView2Environment;
struct ICoreWebView2;

class QWidget;

namespace webview
{
class WebView2Session;
class WebViewState;

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
    WebView2View(QWidget* parent, ICoreWebView2Environment* environment,
        std::shared_ptr<WebViewState> sessionState, WebViewPolicyPtr policy,
        QVector<WebResourceMapping> resourceMappings, SessionMode sessionMode,
        std::function<QString(ICoreWebView2*)> registerProfile);

    class Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace webview
