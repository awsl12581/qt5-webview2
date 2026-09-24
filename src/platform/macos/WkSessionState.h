#pragma once

#include "internal/ResourceMapping.h"
#include "webview/WebViewState.h"

#include <unordered_set>
#include <atomic>
#include <mutex>

namespace webview
{
class WkWebView;

struct WkSessionState {
    std::atomic_bool valid = true;
    InitializationScheduler initialization;
    QVector<ResourceMapping> resourceMappings;
    std::unordered_set<WkWebView*> views;
    std::mutex viewsMutex;
};
} // namespace webview
