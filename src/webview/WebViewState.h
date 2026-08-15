#pragma once

#include "webview/DocumentLifetime.h"
#include "webview/IWebView.h"
#include "webview/WebViewPolicy.h"

#include <functional>
#include <memory>
#include <vector>

namespace webview
{
class WebViewState final : public std::enable_shared_from_this<WebViewState>
{
public:
    WebViewHostCallbacks callbacks;
    WebViewPolicyPtr policy;
    QUrl committedUrl;
    QString documentToken;
    DownloadTarget pendingDownload;
    bool documentTransportPrepared = false;
    bool provisionalMainFrameNavigation = false;
    bool explicitMainFrameNavigationPending = false;
    quint64 navigationId = 0;
    DocumentLifetime lifetime;
    InitializationState initializationState() const;
    void markReady();
    void failInitialization(QString error);
    void whenInitialized(IWebView::InitializationCompletion completion);
    void runWhenReady(std::function<void()> operation);
    void close();

    void emitLoad(LoadState loadState, quint64 eventNavigationId, const QUrl& url = { },
        const QString& error = { });
private:
    InitializationState initializationState_ = InitializationState::Initializing;
    QString initializationError_;
    std::vector<IWebView::InitializationCompletion> initializationCompletions_;
    std::vector<std::function<void()>> readyOperations_;
};
} // namespace webview
