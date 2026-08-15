#pragma once

#include "webview/WebViewTypes.h"
#include "webview/WebViewState.h"

#include <unordered_set>

namespace webview
{
class WkWebView;

struct WkSessionState {
    bool valid = true;
    InitializationScheduler initialization;
    QVector<WebResourceMapping> resourceMappings;
    std::unordered_set<WkWebView*> views;
};
} // namespace webview
