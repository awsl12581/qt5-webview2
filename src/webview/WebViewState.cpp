#include "webview/WebViewState.h"
#include "webview/JsonMessage.h"

namespace webview
{
WebViewState::WebViewState(DiagnosticScope scope, std::uint64_t sessionId)
    : diagnosticScope(scope)
    , diagnosticId(scope == DiagnosticScope::None ? 0 : nextDiagnosticId())
    , sessionDiagnosticId(sessionId)
{
    if (scope == DiagnosticScope::Session) {
        sessionDiagnosticId = diagnosticId;
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.lifecycle") << "event=session.created" << "session=" << diagnosticId;
    }
    else if (scope == DiagnosticScope::View) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.lifecycle") << "event=view.created" << "session=" << sessionDiagnosticId << "view=" << diagnosticId;
    }
    bridge->setDiagnosticContext(sessionDiagnosticId, diagnosticId);
    resources->setDiagnosticContext(sessionDiagnosticId, diagnosticId);
}

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
    finishQueuedOperations({ InitializationState::Ready, {} });
    finishInitializationCompletions({ InitializationState::Ready, {} });
}

void InitializationScheduler::fail(std::string error)
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
    error_ = "The web view is closed.";
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
    if (initialization_.state() != InitializationState::Initializing) {
        return;
    }
    initialization_.markReady();
    if (diagnosticScope == DiagnosticScope::Session) {
        diagnosticLog(DiagnosticLevel::Info, "system_webview.lifecycle") << "event=session.ready" << "session=" << diagnosticId;
    }
    else if (diagnosticScope == DiagnosticScope::View) {
        diagnosticLog(DiagnosticLevel::Info, "system_webview.lifecycle") << "event=view.ready" << "session=" << sessionDiagnosticId << "view=" << diagnosticId;
    }
}

void WebViewState::failInitialization(std::string error)
{
    if (initialization_.state() != InitializationState::Initializing) {
        return;
    }
    initialization_.fail(std::move(error));
    if (diagnosticScope == DiagnosticScope::Session) {
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.lifecycle") << "event=session.initialization_failed" << "session=" << diagnosticId;
    }
    else if (diagnosticScope == DiagnosticScope::View) {
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.lifecycle")
            << "event=view.initialization_failed" << "session=" << sessionDiagnosticId << "view=" << diagnosticId;
    }
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
    if (lifetime.isClosed()) {
        return;
    }
    lifetime.close();
    if (bridge) {
        bridge->invalidate();
    }
    if (resources) {
        resources->releaseAll();
    }
    initialization_.close();
    if (diagnosticScope == DiagnosticScope::Session) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.lifecycle") << "event=session.closed" << "session=" << diagnosticId;
    }
    else if (diagnosticScope == DiagnosticScope::View) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.lifecycle") << "event=view.closed" << "session=" << sessionDiagnosticId << "view=" << diagnosticId;
    }
}

void WebViewState::bindBridgePolicy()
{
    const std::weak_ptr<WebViewState> weak = shared_from_this();
    bridge->setEventHandler("release-resource", [weak](const Json& payload) {
        if (const auto state = weak.lock()) {
            state->resources->release(jsonString(payload, "token"));
        }
    });
    resources->setRevocationHandler([weak](const std::string& token) {
        if (const auto state = weak.lock()) {
            state->bridge->emitEvent("resource-revoked", { { "token", token } });
        }
    });
    bridge->setValidator([weak](const BridgeMessage& message, bool outbound, int wireSize, std::string* error) {
        const auto state = weak.lock();
        const auto reject = [&state, error](const char* reason, const std::string& detail) {
            if (error) {
                *error = detail;
            }
            if (state) {
                diagnosticLog(DiagnosticLevel::Debug, "system_webview.bridge") << "event=bridge.message_rejected" << "session=" << state->sessionDiagnosticId
                                                       << "view=" << state->diagnosticId << "reason=" << reason;
            }
            return false;
        };
        if (!state || state->lifetime.isClosed() || !state->policy || !state->policy->allowsBridge(state->committedUrl)) {
            return reject("unauthorized_document", "The current document is not authorized for bridge messages.");
        }
        if (wireSize > state->policy->maximumBridgeMessageBytes()) {
            return reject("size_limit", "Bridge message exceeds the configured size limit.");
        }
        if (message.type == "release-resource" || message.type == "resource-revoked") {
            const bool allowed = message.kind == BridgeMessageKind::Event
                                 && message.type == (outbound ? "resource-revoked" : "release-resource") && message.payload.size() == 1
                                 && message.payload.contains("token") && message.payload.at("token").is_string()
                                 && !jsonString(message.payload, "token").empty();
            if (!allowed) {
                return reject("resource_control", "Invalid resource control message.");
            }
            return true;
        }
        const bool allowed = outbound || message.kind == BridgeMessageKind::Response
                                 ? state->policy->validateHostToPageMessage(message, error)
                                 : state->policy->validatePageToHostMessage(message, error);
        if (!allowed) {
            diagnosticLog(DiagnosticLevel::Debug, "system_webview.bridge") << "event=bridge.message_rejected" << "session=" << state->sessionDiagnosticId
                                                   << "view=" << state->diagnosticId << "reason=schema";
        }
        return allowed;
    });
}

void WebViewState::invalidateDocument()
{
    lifetime.invalidate();
    if (bridge) {
        bridge->cancelPending("The document changed.");
    }
    setResourceDocumentToken({});
}

void WebViewState::setResourceDocumentToken(std::string_view token)
{
    if (resources) {
        resources->setDocumentToken(std::string(token));
    }
}

void WebViewState::setResourceContext(const Url& origin, const Url& documentOrigin, std::string_view token)
{
    if (resources) {
        resources->setContext(origin, documentOrigin, std::string(token));
    }
}

void WebViewState::emitLoad(LoadState loadState, std::uint64_t eventNavigationId, const Url& url, std::string_view error)
{
    if (loadState == LoadState::Failed) {
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.navigation")
            << "event=navigation.failed" << "session=" << sessionDiagnosticId << "view=" << diagnosticId
            << "navigation=" << eventNavigationId << "origin=" << diagnosticOrigin(url);
    }
    if (!lifetime.isClosed() && callbacks.onLoad) {
        callbacks.onLoad({ loadState, url, std::string(error), eventNavigationId, true });
    }
}

void WebViewState::emitRuntimeFailure(const RuntimeFailureEvent& event)
{
    if (!lifetime.isClosed() && callbacks.onRuntimeFailure) {
        callbacks.onRuntimeFailure(event);
    }
}
} // namespace webview
