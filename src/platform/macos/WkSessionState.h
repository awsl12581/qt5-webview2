#pragma once

#include "webview/WebViewTypes.h"

#include <unordered_set>

namespace webview
{
class WkWebView;

struct WkSessionState {
    bool valid = true;
    QVector<WebResourceMapping> resourceMappings;
    std::unordered_set<WkWebView*> views;
};
} // namespace webview
