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
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebView() = default;
    virtual QWidget* widget() = 0;
    virtual InitializationState initializationState() const = 0;
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    virtual void attachNativeView() = 0;
    virtual void detachNativeView() = 0;
    virtual void open(WebApplicationPtr application, const QString& route = { }) = 0;
    virtual void navigate(const QUrl& url) = 0;
    virtual void loadDocument(const QString& html, const QUrl& baseUrl = { }) = 0;
    virtual void stop() = 0;
    virtual void reload() = 0;
    virtual void close() = 0;
    virtual bool isClosed() const = 0;
    virtual void sendMessage(const BridgeMessage& message, MessageCompletion completion = { }) = 0;
    virtual void setHostCallbacks(WebViewHostCallbacks callbacks) = 0;
};

} // namespace webview
