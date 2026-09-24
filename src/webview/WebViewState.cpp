#include "webview/WebViewState.h"

namespace webview
{
InitializationState InitializationScheduler::state() const
{
    return state_;
}

void InitializationScheduler::markReady()
{
    if (state_ != InitializationState::Initializing) {
        return;
    }
    state_ = InitializationState::Ready;
    finishQueuedOperations({ InitializationState::Ready, { } });
    finishInitializationCompletions({ InitializationState::Ready, { } });
}

void InitializationScheduler::fail(QString error)
{
    if (state_ != InitializationState::Initializing) {
        return;
    }
    error_ = std::move(error);
    state_ = InitializationState::Failed;
    const InitializationResult result { state_, error_ };
    finishQueuedOperations(result);
    finishInitializationCompletions(result);
}

void InitializationScheduler::whenInitialized(Completion completion)
{
    if (!completion) {
        return;
    }
    if (state_ == InitializationState::Initializing) {
        initializationCompletions_.push_back(std::move(completion));
        return;
    }
    completion({ state_, error_ });
}

void InitializationScheduler::runWhenReady(Completion operation)
{
    if (!operation) {
        return;
    }
    if (state_ == InitializationState::Initializing || drainingReadyOperations_) {
        readyOperations_.push_back(std::move(operation));
        return;
    }
    operation({ state_, error_ });
}

void InitializationScheduler::close()
{
    if (state_ == InitializationState::Closed) {
        return;
    }
    state_ = InitializationState::Closed;
    error_ = QStringLiteral("The web view is closed.");
    const InitializationResult result { state_, error_ };
    finishQueuedOperations(result);
    finishInitializationCompletions(result);
}

void InitializationScheduler::finishQueuedOperations(const InitializationResult& result)
{
    drainingReadyOperations_ = result.state == InitializationState::Ready;
    while (!readyOperations_.empty()) {
        auto operations = std::move(readyOperations_);
        readyOperations_.clear();
        for (auto& operation : operations) {
            if (operation) {
                operation(result);
            }
        }
    }
    drainingReadyOperations_ = false;
}

void InitializationScheduler::finishInitializationCompletions(const InitializationResult& result)
{
    auto completions = std::move(initializationCompletions_);
    initializationCompletions_.clear();
    for (auto& completion : completions) {
        if (completion) {
            completion(result);
        }
    }
}

InitializationState WebViewState::initializationState() const
{
    return initialization_.state();
}

void WebViewState::markReady()
{
    initialization_.markReady();
}

void WebViewState::failInitialization(QString error)
{
    initialization_.fail(std::move(error));
}

void WebViewState::whenInitialized(IWebView::InitializationCompletion completion)
{
    initialization_.whenInitialized(std::move(completion));
}

void WebViewState::runWhenReady(std::function<void(const InitializationResult&)> operation)
{
    initialization_.runWhenReady(std::move(operation));
}

void WebViewState::close()
{
    lifetime.close();
    if (bridge) bridge->invalidate();
    if (resources) resources->releaseAll();
    initialization_.close();
}

void WebViewState::bindBridgePolicy()
{
    const std::weak_ptr<WebViewState> weak = shared_from_this();
    bridge->on(QStringLiteral("release-resource"), [weak](const QJsonObject& payload) {
        if (const auto state = weak.lock()) state->resources->release(payload.value(QStringLiteral("token")).toString());
    });
    resources->setRevocationHandler([weak](const QString& token) {
        if (const auto state = weak.lock()) {
            state->bridge->emitEvent(QStringLiteral("resource-revoked"), { { QStringLiteral("token"), token } });
        }
    });
    bridge->setValidator([weak](const BridgeMessage& message, bool outbound, int wireSize, QString* error) {
        const auto state = weak.lock();
        if (!state || state->lifetime.isClosed() || !state->policy
            || !state->policy->allowsBridge(state->committedUrl)) {
            if (error) *error = QStringLiteral("The current document is not authorized for bridge messages.");
            return false;
        }
        if (wireSize > state->policy->maximumBridgeMessageBytes()) {
            if (error) *error = QStringLiteral("Bridge message exceeds the configured size limit.");
            return false;
        }
        if (message.type == QStringLiteral("release-resource")
            || message.type == QStringLiteral("resource-revoked")) {
            const bool allowed = message.kind == BridgeMessageKind::Event
                && message.type == (outbound ? QStringLiteral("resource-revoked") : QStringLiteral("release-resource"))
                && message.payload.size() == 1
                && message.payload.value(QStringLiteral("token")).isString()
                && !message.payload.value(QStringLiteral("token")).toString().isEmpty();
            if (!allowed && error) *error = QStringLiteral("Invalid resource control message.");
            return allowed;
        }
        return outbound || message.kind == BridgeMessageKind::Response
            ? state->policy->validateHostToPageMessage(message, error)
            : state->policy->validatePageToHostMessage(message, error);
    });
}

void WebViewState::invalidateDocument()
{
    lifetime.invalidate();
    if (bridge) bridge->cancelPending(QStringLiteral("The document changed."));
    setResourceDocumentToken({});
}

void WebViewState::setResourceDocumentToken(const QString& token)
{
    if (resources) resources->setDocumentToken(token);
}

void WebViewState::setResourceContext(const QUrl& origin, const QUrl& documentOrigin, const QString& token)
{
    if (resources) resources->setContext(origin, documentOrigin, token);
}

void WebViewState::emitLoad(LoadState loadState, quint64 eventNavigationId, const QUrl& url,
    const QString& error)
{
    if (!lifetime.isClosed() && callbacks.load) {
        callbacks.load({ loadState, url, error, eventNavigationId, true });
    }
}
} // namespace webview
