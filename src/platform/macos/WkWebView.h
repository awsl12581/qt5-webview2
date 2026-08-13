#pragma once

#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

#include <memory>

namespace webview
{
class WkWebViewSession;
struct WkSessionState;

class WkWebView final : public IWebView
{
public:
    ~WkWebView() override;

    QWidget* widget() override;
    void load(const QUrl& url) override;
    void setHtml(const QString& html, const QUrl& baseUrl) override;
    void stop() override;
    void reload() override;
    void close() override;
    bool isClosed() const override;
    void sendMessage(const BridgeMessage& message, MessageCompletion completion) override;
    void setHostCallbacks(WebViewHostCallbacks callbacks) override;

    void* nativeConfigurationForTesting() const;
    QString documentTokenForTesting() const;
    bool isNativeViewAttachedForTesting() const;
    QSize nativeViewSizeForTesting() const;
    void attachNativeView();

private:
    friend class WkWebViewSession;
    WkWebView(QWidget* parent, void* configuration, WebViewPolicyPtr policy,
        std::shared_ptr<WkSessionState> sessionState);
    void initialize(void* configuration, WebViewPolicyPtr policy,
        std::shared_ptr<WkSessionState> sessionState);

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
