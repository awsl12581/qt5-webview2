#pragma once

#include "webview/WebViewTypes.h"
#include "webview/WebViewState.h"

#include <unordered_set>
#include <atomic>

namespace webview
{
class WkWebView;

struct WkSessionState {
    std::atomic_bool valid = true;
    InitializationScheduler initialization;
    QVector<WebResourceMapping> resourceMappings;
    std::unordered_set<WkWebView*> views;
};
} // namespace webview
