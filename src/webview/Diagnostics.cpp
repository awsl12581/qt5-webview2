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
quint64 nextDiagnosticId()
{
    static std::atomic<quint64> nextId { 1 };
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

QString diagnosticOrigin(const QUrl& url)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        return QStringLiteral("invalid");
    }
    QUrl origin;
    origin.setScheme(url.scheme().toLower());
    origin.setHost(url.host().toLower());
    origin.setPort(url.port());
    return origin.toString(QUrl::FullyEncoded);
}
} // namespace webview
