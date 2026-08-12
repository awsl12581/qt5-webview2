#pragma once

#include <QJsonObject>
#include <QUrl>

#include <functional>
#include <memory>

class QWidget;

namespace webview
{
class IWebView;
using WebViewPtr = std::unique_ptr<IWebView>;

class IWebView
{
public:
    using MessageHandler = std::function<void(const QJsonObject&)>;
    using NewWindowHandler = std::function<void(WebViewPtr)>;

    virtual ~IWebView() = default;
    virtual QWidget* widget() = 0;
    virtual void load(const QUrl& url) = 0;
    virtual void setHtml(const QString& html, const QUrl& baseUrl = { }) = 0;
    virtual void postMessage(const QJsonObject& message) = 0;
    virtual void setMessageHandler(MessageHandler handler) = 0;
    virtual void setNewWindowHandler(NewWindowHandler handler) = 0;
};

} // namespace webview
