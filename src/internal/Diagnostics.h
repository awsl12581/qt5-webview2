#pragma once

#include <QLoggingCategory>
#include <QString>
#include <QUrl>

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

quint64 nextDiagnosticId();
QString diagnosticOrigin(const QUrl& url);
} // namespace webview
