#pragma once

#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

#include <memory>

namespace webview
{
class WkWebView final : public IWebView
{
public:
    WkWebView(QWidget* parent, WebViewPolicyPtr policy);
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

private:
    WkWebView(QWidget* parent, void* configuration, WebViewPolicyPtr policy);
    void initialize(void* configuration, WebViewPolicyPtr policy);

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
