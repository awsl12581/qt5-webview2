#pragma once

#include "webview/IWebView.h"

#include <memory>

namespace webview
{
class WkWebView final : public IWebView
{
public:
    explicit WkWebView(QWidget* parent);
    ~WkWebView() override;

    QWidget* widget() override;
    void load(const QUrl& url) override;
    void setHtml(const QString& html, const QUrl& baseUrl) override;
    void postMessage(const QJsonObject& message) override;
    void setMessageHandler(MessageHandler handler) override;
    void setNewWindowHandler(NewWindowHandler handler) override;

private:
    WkWebView(QWidget* parent, void* configuration);
    void initialize(void* configuration);

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
