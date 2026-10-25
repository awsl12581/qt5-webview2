#include "internal/Diagnostics.h"

#include <atomic>

Q_LOGGING_CATEGORY(systemWebViewLifecycle, "system_webview.lifecycle", QtInfoMsg)
Q_LOGGING_CATEGORY(systemWebViewNavigation, "system_webview.navigation", QtInfoMsg)
Q_LOGGING_CATEGORY(systemWebViewPolicy, "system_webview.policy", QtInfoMsg)
Q_LOGGING_CATEGORY(systemWebViewBridge, "system_webview.bridge", QtInfoMsg)
Q_LOGGING_CATEGORY(systemWebViewResource, "system_webview.resource", QtInfoMsg)
Q_LOGGING_CATEGORY(systemWebViewRuntime, "system_webview.runtime", QtInfoMsg)

namespace webview
{
std::uint64_t nextDiagnosticId()
{
    static std::atomic<std::uint64_t> nextId { 1 };
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

std::string diagnosticOrigin(const Url& url)
{
    if (!url.isValid() || url.scheme().empty()) {
        return "invalid";
    }
    return url.origin().toString();
}
} // namespace webview
