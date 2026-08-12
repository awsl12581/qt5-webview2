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

    virtual ~IWebViewSession() = default;
    virtual WebViewPtr createWebView(QWidget* parent = nullptr) = 0;
    virtual void clearCache(ClearCompletion completion = { }) = 0;
    virtual void clearCookies(ClearCompletion completion = { }) = 0;
    virtual void clearWebsiteData(ClearCompletion completion = { }) = 0;
    virtual CapabilitySupport permissionSupport(PermissionKind kind) const = 0;
    virtual CapabilitySupport downloadSupport() const = 0;
};

using WebViewSessionPtr = std::unique_ptr<IWebViewSession>;
} // namespace webview
