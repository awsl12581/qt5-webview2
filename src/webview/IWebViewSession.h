#pragma once

#include "webview/WebViewTypes.h"

#include <functional>
#include <memory>

class QWidget;

namespace webview
{
class IWebViewSession
{
public:
    using ClearCompletion = std::function<void(const WebsiteDataResult&)>;
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebViewSession() = default;
    virtual InitializationState initializationState() const = 0;
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    virtual WebApplicationPtr createApplication(WebApplicationOptions options) = 0;
    virtual WebViewPtr createWebView(QWidget* parent = nullptr) = 0;
    virtual void clearCache(ClearCompletion completion = { }) = 0;
    virtual void clearCookies(ClearCompletion completion = { }) = 0;
    virtual void clearWebsiteData(ClearCompletion completion = { }) = 0;
    virtual CapabilitySupport capabilitySupport(WebViewCapability capability) const = 0;
};

using WebViewSessionPtr = std::unique_ptr<IWebViewSession>;
} // namespace webview
