#pragma once

#include "internal/Diagnostics.h"
#include "internal/ResourceMapping.h"
#include "webview/WebViewState.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace webview
{
class WkWebView;

struct WkSessionState
{
    WkSessionState()
        : diagnosticId(nextDiagnosticId())
    {
    }

    std::atomic_bool valid = true;
    quint64 diagnosticId = 0;
    InitializationScheduler initialization;
    WebViewSessionHostCallbacks callbacks;
    QVector<ResourceMapping> resourceMappings;
    std::unordered_set<WkWebView*> views;
    std::mutex viewsMutex;
};
} // namespace webview
