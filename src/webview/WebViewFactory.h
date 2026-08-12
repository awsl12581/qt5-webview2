#pragma once

#include "webview/IWebViewSession.h"
#include "webview/WebViewPolicy.h"

#include <QString>

namespace webview
{
WebViewSessionPtr createPersistentSession(const QString& profilePath, WebViewPolicyPtr policy = { });
WebViewSessionPtr createEphemeralSession(WebViewPolicyPtr policy = { });
} // namespace webview
