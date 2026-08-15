#include "webview/WebViewState.h"

namespace webview
{
InitializationState WebViewState::initializationState() const
{
    return initializationState_;
}

void WebViewState::markReady()
{
    if (initializationState_ != InitializationState::Initializing) {
        return;
    }
    initializationState_ = InitializationState::Ready;
    const auto completions = std::move(initializationCompletions_);
    for (const auto& completion : completions) {
        if (completion) {
            completion({ InitializationState::Ready, { } });
        }
    }
    const auto operations = std::move(readyOperations_);
    for (const auto& operation : operations) {
        if (operation) {
            operation();
        }
    }
}

void WebViewState::failInitialization(QString error)
{
    if (initializationState_ != InitializationState::Initializing) {
        return;
    }
    initializationError_ = std::move(error);
    initializationState_ = InitializationState::Failed;
    readyOperations_.clear();
    const auto completions = std::move(initializationCompletions_);
    for (const auto& completion : completions) {
        if (completion) {
            completion({ InitializationState::Failed, initializationError_ });
        }
    }
}

void WebViewState::whenInitialized(IWebView::InitializationCompletion completion)
{
    if (!completion) {
        return;
    }
    if (initializationState_ == InitializationState::Initializing) {
        initializationCompletions_.push_back(std::move(completion));
        return;
    }
    completion({ initializationState_, initializationError_ });
}

void WebViewState::runWhenReady(std::function<void()> operation)
{
    if (!operation) {
        return;
    }
    if (initializationState_ == InitializationState::Initializing) {
        readyOperations_.push_back(std::move(operation));
        return;
    }
    if (initializationState_ == InitializationState::Ready) {
        operation();
    }
}

void WebViewState::close()
{
    lifetime.close();
    initializationState_ = InitializationState::Closed;
    readyOperations_.clear();
    const auto completions = std::move(initializationCompletions_);
    for (const auto& completion : completions) {
        if (completion) {
            completion({ InitializationState::Closed, QStringLiteral("The web view is closed.") });
        }
    }
}

void WebViewState::emitLoad(LoadState loadState, quint64 eventNavigationId, const QUrl& url,
    const QString& error)
{
    if (!lifetime.isClosed() && callbacks.load) {
        callbacks.load({ loadState, url, error, eventNavigationId, true });
    }
}

} // namespace webview
