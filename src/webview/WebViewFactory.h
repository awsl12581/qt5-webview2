#pragma once

#include "webview/IWebView.h"

namespace webview
{
WebViewPtr createWebView(QWidget* parent = nullptr);
} // namespace webview
