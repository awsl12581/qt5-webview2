#pragma once

#include "internal/Diagnostics.h"
#include "internal/ResourceMapping.h"
#include "webview/WebViewState.h"

#include <atomic>
#include <cstdint>
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
    std::uint64_t diagnosticId = 0;
    InitializationScheduler initialization;
    WebViewSessionHostCallbacks callbacks;
    std::vector<ResourceMapping> resourceMappings;
    std::unordered_set<WkWebView*> views;
    std::mutex viewsMutex;
};
} // namespace webview
