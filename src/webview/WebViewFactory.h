#pragma once

#include "webview/IWebViewSession.h"
#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

namespace webview
{
WebViewSessionPtr createWebViewSession(WebViewSessionOptions options, WebViewPolicyPtr policy = { });
} // namespace webview
