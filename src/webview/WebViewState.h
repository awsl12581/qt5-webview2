#pragma once

#include "internal/Diagnostics.h"
#include "webview/DocumentLifetime.h"

#include <system_webview/system_webview.h>

#include <functional>
#include <memory>
#include <vector>

namespace webview
{
class InitializationScheduler final
{
public:
    using Completion = std::function<void(const InitializationResult&)>;

    InitializationState state() const;
    void markReady();
    void fail(QString error);
    void whenInitialized(Completion completion);
    void runWhenReady(Completion operation);
    void close();

private:
    void finishQueuedOperations(const InitializationResult& result);
    void finishInitializationCompletions(const InitializationResult& result);

    InitializationState state_ = InitializationState::Initializing;
    QString error_;
    std::vector<Completion> initializationCompletions_;
    std::vector<Completion> readyOperations_;
    bool drainingReadyOperations_ = false;
};

class WebViewState final : public std::enable_shared_from_this<WebViewState>
{
public:
    explicit WebViewState(DiagnosticScope diagnosticScope = DiagnosticScope::None, quint64 sessionDiagnosticId = 0);

    WebViewHostCallbacks callbacks;
    std::shared_ptr<WebViewBridge> bridge = std::make_shared<WebViewBridge>();
    std::shared_ptr<WebResourceManager> resources = std::make_shared<WebResourceManager>();
    WebViewPolicyPtr policy;
    QUrl committedUrl;
    QUrl bridgeOrigin;
    QUrl resourceOrigin;
    QString documentToken;
    bool documentTransportPrepared = false;
    bool provisionalMainFrameNavigation = false;
    bool explicitMainFrameNavigationPending = false;
    quint64 navigationId = 0;
    DiagnosticScope diagnosticScope = DiagnosticScope::None;
    quint64 diagnosticId = 0;
    quint64 sessionDiagnosticId = 0;
    DocumentLifetime lifetime;
    InitializationState initializationState() const;
    void markReady();
    void failInitialization(QString error);
    void whenInitialized(IWebView::InitializationCompletion completion);
    void runWhenReady(std::function<void(const InitializationResult&)> operation);
    void close();
    void bindBridgePolicy();
    void invalidateDocument();
    void setResourceDocumentToken(const QString& token);
    void setResourceContext(const QUrl& origin, const QUrl& documentOrigin, const QString& token);

    void emitLoad(LoadState loadState, quint64 eventNavigationId, const QUrl& url = { }, const QString& error = { });
    void emitRuntimeFailure(const RuntimeFailureEvent& event);

private:
    InitializationScheduler initialization_;
};
} // namespace webview
