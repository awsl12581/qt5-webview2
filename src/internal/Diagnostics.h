#pragma once

#include <QLoggingCategory>
#include <QUrl>
#include <cstdint>
#include <string>

Q_DECLARE_LOGGING_CATEGORY(systemWebViewLifecycle)
Q_DECLARE_LOGGING_CATEGORY(systemWebViewNavigation)
Q_DECLARE_LOGGING_CATEGORY(systemWebViewPolicy)
Q_DECLARE_LOGGING_CATEGORY(systemWebViewBridge)
Q_DECLARE_LOGGING_CATEGORY(systemWebViewResource)
Q_DECLARE_LOGGING_CATEGORY(systemWebViewRuntime)

namespace webview
{
enum class DiagnosticScope
{
    None,
    Session,
    View
};

std::uint64_t nextDiagnosticId();
std::string diagnosticOrigin(const QUrl& url);
} // namespace webview
