#pragma once

#include "webview/WebViewTypes.h"

#include <functional>

class QWidget;

namespace webview
{
class IWebView
{
public:
    using MessageCompletion = std::function<void(const MessageResult&)>;

    virtual ~IWebView() = default;
    virtual QWidget* widget() = 0;
    virtual void load(const QUrl& url) = 0;
    virtual void setHtml(const QString& html, const QUrl& baseUrl = { }) = 0;
    virtual void stop() = 0;
    virtual void reload() = 0;
    virtual void close() = 0;
    virtual bool isClosed() const = 0;
    virtual void sendMessage(const BridgeMessage& message, MessageCompletion completion = { }) = 0;
    virtual void setHostCallbacks(WebViewHostCallbacks callbacks) = 0;
};

} // namespace webview
