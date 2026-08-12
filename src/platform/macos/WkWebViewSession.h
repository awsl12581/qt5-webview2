#pragma once

#include "webview/IWebViewSession.h"
#include "webview/WebViewPolicy.h"

#include <memory>

namespace webview
{
class WkWebViewSession final : public IWebViewSession
{
public:
    WkWebViewSession(QString profilePath, bool ephemeral, WebViewPolicyPtr policy);
    ~WkWebViewSession() override;

    WebViewPtr createWebView(QWidget* parent) override;
    void clearCache(ClearCompletion completion) override;
    void clearCookies(ClearCompletion completion) override;
    void clearWebsiteData(ClearCompletion completion) override;

    void* nativeConfigurationForTesting() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
