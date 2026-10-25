#pragma once

#include "internal/Diagnostics.h"
#include "webview/DocumentLifetime.h"

#include <system_webview/system_webview.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace webview
{
class InitializationScheduler final
{
public:
    using Completion = std::function<void(const InitializationResult&)>;

    InitializationState state() const;
    void markReady();
    void fail(std::string error);
    void whenInitialized(Completion completion);
    void runWhenReady(Completion operation);
    void close();

private:
    void finishQueuedOperations(const InitializationResult& result);
    void finishInitializationCompletions(const InitializationResult& result);

    InitializationState state_ = InitializationState::Initializing;
    std::string error_;
    std::vector<Completion> initializationCompletions_;
    std::vector<Completion> readyOperations_;
    bool drainingReadyOperations_ = false;
};

class WebViewState final : public std::enable_shared_from_this<WebViewState>
{
public:
    explicit WebViewState(DiagnosticScope diagnosticScope = DiagnosticScope::None, std::uint64_t sessionDiagnosticId = 0);

    WebViewHostCallbacks callbacks;
    std::shared_ptr<WebViewBridge> bridge = std::make_shared<WebViewBridge>();
    std::shared_ptr<WebResourceManager> resources = std::make_shared<WebResourceManager>();
    WebViewPolicyPtr policy;
    Url committedUrl;
    Url bridgeOrigin;
    Url resourceOrigin;
    std::string documentToken;
    bool documentTransportPrepared = false;
    bool provisionalMainFrameNavigation = false;
    bool explicitMainFrameNavigationPending = false;
    std::uint64_t navigationId = 0;
    DiagnosticScope diagnosticScope = DiagnosticScope::None;
    std::uint64_t diagnosticId = 0;
    std::uint64_t sessionDiagnosticId = 0;
    DocumentLifetime lifetime;
    InitializationState initializationState() const;
    void markReady();
    void failInitialization(std::string error);
    void whenInitialized(IWebView::InitializationCompletion completion);
    void runWhenReady(std::function<void(const InitializationResult&)> operation);
    void close();
    void bindBridgePolicy();
    void invalidateDocument();
    void setResourceDocumentToken(std::string_view token);
    void setResourceContext(const Url& origin, const Url& documentOrigin, std::string_view token);

    void emitLoad(LoadState loadState, std::uint64_t eventNavigationId, const Url& url = {}, std::string_view error = {});
    void emitRuntimeFailure(const RuntimeFailureEvent& event);

private:
    InitializationScheduler initialization_;
};
} // namespace webview
