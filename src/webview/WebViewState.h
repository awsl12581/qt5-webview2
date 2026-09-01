#pragma once

#include "webview/DocumentLifetime.h"
#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

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
    WebViewHostCallbacks callbacks;
    WebViewPolicyPtr policy;
    QUrl committedUrl;
    QUrl bridgeOrigin;
    QString documentToken;
    bool documentTransportPrepared = false;
    bool provisionalMainFrameNavigation = false;
    bool explicitMainFrameNavigationPending = false;
    quint64 navigationId = 0;
    DocumentLifetime lifetime;
    InitializationState initializationState() const;
    void markReady();
    void failInitialization(QString error);
    void whenInitialized(IWebView::InitializationCompletion completion);
    void runWhenReady(std::function<void(const InitializationResult&)> operation);
    void close();

    void emitLoad(LoadState loadState, quint64 eventNavigationId, const QUrl& url = { },
        const QString& error = { });
private:
    InitializationScheduler initialization_;
};
} // namespace webview
