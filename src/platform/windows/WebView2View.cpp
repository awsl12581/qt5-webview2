#include "platform/windows/WebView2View.h"

#include "webview/WebViewState.h"
#include "webview/HostCompletion.h"
#include "webview/JsonMessage.h"
#include "webview/ResourceMapping.h"

#include <QMetaObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QMimeDatabase>
#include <QPointer>
#include <QUuid>
#include <QWidget>
#include <QResizeEvent>
#include <QThread>

#include <WebView2.h>
#include <windows.h>
#include <wrl.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace webview
{
namespace {
class NativeViewHost final : public QWidget
{
public:
    std::function<void()> resized;
    explicit NativeViewHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (resized) resized();
    }
};
}

class WebView2View::Impl : public std::enable_shared_from_this<WebView2View::Impl>
{
public:
    struct PendingAction {
        std::atomic_bool completed = false;
        std::function<void()> cancel;

        bool finish(const std::function<void()>& action)
        {
            if (completed.exchange(true)) return false;
            if (action) action();
            return true;
        }
    };

    explicit Impl(QWidget* parent, std::function<ICoreWebView2Environment*()> environmentProvider,
        std::shared_ptr<WebViewState> sessionState, WebViewPolicyPtr policy,
        QVector<WebResourceMapping> resourceMappings, SessionMode sessionMode,
        std::function<QString(ICoreWebView2*)> registerProfile,
        std::function<void(std::function<void()>)> registerSessionClose)
        : container(new NativeViewHost(parent)), state(std::make_shared<WebViewState>()),
          sessionState(std::move(sessionState)), environmentProvider(std::move(environmentProvider)), policy(std::move(policy)),
          resourceMappings(std::move(resourceMappings)), sessionMode(sessionMode),
          registerProfile(std::move(registerProfile)), registerSessionClose(std::move(registerSessionClose)),
          inlineDocuments(std::make_shared<QHash<QString, QByteArray>>()), ownsContainer(parent == nullptr)
    {
        state->policy = this->policy;
    }

    void start()
    {
        if (registerSessionClose) {
            registerSessionClose([weak = weak_from_this()] {
                if (const auto owner = weak.lock()) owner->close();
            });
        }
        container->resized = [weak = weak_from_this()] {
            if (const auto owner = weak.lock()) owner->applyHostState();
        };
        sessionState->runWhenReady([weak = weak_from_this()](const InitializationResult& result) {
            const auto owner = weak.lock();
            if (!owner || owner->state->lifetime.isClosed()) return;
            if (result.state != InitializationState::Ready) {
                owner->state->failInitialization(result.error);
                return;
            }
            owner->environment = owner->environmentProvider ? owner->environmentProvider() : nullptr;
            if (!owner->environment) {
                owner->state->failInitialization(QStringLiteral("WebView2 environment is unavailable."));
                return;
            }
            const HWND hwnd = reinterpret_cast<HWND>(owner->container->winId());
            auto completed = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [state = owner->state, owner](HRESULT hr, ICoreWebView2Controller* created) -> HRESULT {
                        if (state->lifetime.isClosed()) return S_OK;
                        auto& controller = owner->controller;
                        auto& webview = owner->webview;
                        auto& webResourceToken = owner->webResourceToken;
                        auto& permissionToken = owner->permissionToken;
                        auto& newWindowToken = owner->newWindowToken;
                        auto& downloadToken = owner->downloadToken;
                        auto& navigationStartingToken = owner->navigationStartingToken;
                        auto& sourceChangedToken = owner->sourceChangedToken;
                        auto& contentLoadingToken = owner->contentLoadingToken;
                        auto& navigationCompletedToken = owner->navigationCompletedToken;
                        auto& webMessageToken = owner->webMessageToken;
                        if (FAILED(hr) || !created) {
                            state->failInitialization(QStringLiteral("WebView2 controller creation failed (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(hr), 16)));
                            return S_OK;
                        }
                        controller = created;
                        const HRESULT coreResult = controller->get_CoreWebView2(webview.GetAddressOf());
                        if (FAILED(coreResult) || !webview) {
                            state->failInitialization(QStringLiteral("WebView2 core object unavailable (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(coreResult), 16)));
                            return S_OK;
                        }
                        const QString profileError = owner->registerProfile
                            ? owner->registerProfile(webview.Get())
                            : QStringLiteral("WebView2 profile registration is unavailable.");
                        if (!profileError.isEmpty()) {
                            controller->Close();
                            controller.Reset();
                            webview.Reset();
                            state->failInitialization(profileError);
                            return S_OK;
                        }
                        const auto environmentForRequests = owner->environment;
                        const auto mappingsForRequests = owner->resourceMappings;
                        const auto inlineDocumentsForRequests = owner->inlineDocuments;
                        const auto sessionForChildren = owner->sessionState;
                        const auto policyForChildren = owner->policy;
                        const auto sessionModeForChildren = owner->sessionMode;
                        const auto profileRegistrarForChildren = owner->registerProfile;
                        const auto sessionCloseRegistrarForChildren = owner->registerSessionClose;
                        webview->AddWebResourceRequestedFilter(L"app://*/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                        webview->add_WebResourceRequested(
                            Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                [state = state, environment = environmentForRequests, mappings = mappingsForRequests,
                                    inlineDocuments = inlineDocumentsForRequests](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
                                    if (FAILED(args->get_Request(request.GetAddressOf())) || !request) return S_OK;
                                    LPWSTR rawUri = nullptr;
                                    request->get_Uri(&rawUri);
                                    const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                    CoTaskMemFree(rawUri);
                                    QByteArray bytes;
                                    QString mime = QStringLiteral("text/html");
                                    const auto inlineDocument = inlineDocuments->constFind(url.toString(QUrl::FullyEncoded));
                                    if (inlineDocument != inlineDocuments->cend()) {
                                        bytes = inlineDocument.value();
                                    } else {
                                        const auto* mapping = findResourceMapping(mappings, url);
                                        QString error;
                                        const QString path = mapping ? resolveMappedResource(*mapping, url, &error) : QString();
                                        if (path.isEmpty()) return S_OK;
                                        QFile file(path);
                                        if (!file.open(QIODevice::ReadOnly)) return S_OK;
                                        bytes = file.readAll();
                                        mime = QMimeDatabase().mimeTypeForFile(path).name();
                                    }
                                    Microsoft::WRL::ComPtr<IStream> stream(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.constData()), static_cast<UINT>(bytes.size())));
                                    if (!stream) return S_OK;
                                    const std::wstring headers = std::wstring(L"Content-Type: ") + mime.toStdWString() + L"\r\n";
                                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
                                    if (SUCCEEDED(environment->CreateWebResourceResponse(stream.Get(), 200, L"OK", headers.c_str(), &response)) && response) {
                                        args->put_Response(response.Get());
                                    }
                                    return S_OK;
                                }).Get(), &webResourceToken);
                        webview->add_PermissionRequested(
                            Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                [state = state](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                                    LPWSTR rawUri = nullptr;
                                    COREWEBVIEW2_PERMISSION_KIND nativeKind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
                                    args->get_Uri(&rawUri);
                                    args->get_PermissionKind(&nativeKind);
                                    const QUrl origin = normalizedOrigin(QUrl(QString::fromWCharArray(rawUri ? rawUri : L"")));
                                    CoTaskMemFree(rawUri);
                                    PermissionKind kind = PermissionKind::Notifications;
                                    if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE) kind = PermissionKind::Microphone;
                                    else if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_CAMERA) kind = PermissionKind::Camera;
                                    else if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION) kind = PermissionKind::Location;
                                    else if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ) kind = PermissionKind::Clipboard;
                                    const auto decision = state->policy ? state->policy->decidePermission({ kind, origin }) : PermissionDecision::Deny;
                                    args->put_State(decision == PermissionDecision::Allow ? COREWEBVIEW2_PERMISSION_STATE_ALLOW : COREWEBVIEW2_PERMISSION_STATE_DENY);
                                    return S_OK;
                                }).Get(), &permissionToken);
                        webview->add_NewWindowRequested(
                            Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [state = state, environment = environmentForRequests, sessionState = sessionForChildren,
                                    policy = policyForChildren, mappings = mappingsForRequests,
                                    sessionMode = sessionModeForChildren,
                                    registerProfile = profileRegistrarForChildren,
                                    registerSessionClose = sessionCloseRegistrarForChildren,
                                    weakOwner = owner->weak_from_this()](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
                                    args->GetDeferral(deferral.GetAddressOf());
                                    LPWSTR rawUri = nullptr;
                                    args->get_Uri(&rawUri);
                                    const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                    CoTaskMemFree(rawUri);
                                    const NewWindowRequest request { url, false };
                                    const auto decision = state->policy ? state->policy->decideNewWindow(request) : NewWindowDecision::Cancel;
                                    if (decision != NewWindowDecision::Allow || !state->callbacks.newWindow) {
                                        args->put_Handled(TRUE);
                                        if (deferral) deferral->Complete();
                                        return S_OK;
                                    }
                                    auto childEnvironment = [environment]() -> ICoreWebView2Environment* { return environment.Get(); };
                                    auto child = std::unique_ptr<IWebView>(new WebView2View(nullptr,
                                        std::move(childEnvironment), sessionState, policy, mappings,
                                        sessionMode, registerProfile, registerSessionClose));
                                    auto* childView = static_cast<WebView2View*>(child.get());
                                    auto childHolder = std::make_shared<WebViewPtr>(std::move(child));
                                    Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs> argsRef(args);
                                    auto pending = std::make_shared<PendingAction>();
                                    pending->cancel = [argsRef, deferral, childHolder]() mutable {
                                        if (childHolder && *childHolder) (*childHolder)->close();
                                        if (childHolder) childHolder->reset();
                                        argsRef->put_Handled(TRUE);
                                        if (deferral) deferral->Complete();
                                    };
                                    if (const auto owner = weakOwner.lock()) owner->pendingActions.push_back(pending);
                                    childView->whenInitialized([state, sessionState, request, childHolder, argsRef, deferral,
                                                                  pending, weakOwner](const InitializationResult& result) mutable {
                                        pending->finish([&] {
                                            if (state->lifetime.isClosed() || sessionState->lifetime.isClosed()) {
                                                if (childHolder && *childHolder) (*childHolder)->close();
                                                if (childHolder) childHolder->reset();
                                                argsRef->put_Handled(TRUE);
                                            } else if (result.state == InitializationState::Ready && childHolder && *childHolder) {
                                                auto* readyChild = static_cast<WebView2View*>(childHolder->get());
                                                argsRef->put_NewWindow(readyChild->impl_->webview.Get());
                                                argsRef->put_Handled(TRUE);
                                                if (state->callbacks.newWindow) state->callbacks.newWindow(request, std::move(*childHolder));
                                            } else {
                                                if (childHolder && *childHolder) (*childHolder)->close();
                                                if (childHolder) childHolder->reset();
                                                argsRef->put_Handled(TRUE);
                                            }
                                            if (deferral) deferral->Complete();
                                        });
                                        if (const auto owner = weakOwner.lock()) owner->removePendingAction(pending);
                                        else if (!pending->completed) {
                                            argsRef->put_Handled(TRUE);
                                            if (deferral) deferral->Complete();
                                        }
                                    });
                                    return S_OK;
                                }).Get(), &newWindowToken);
                        Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
                        if (SUCCEEDED(webview.As(&webview4)) && webview4) {
                            webview4->add_DownloadStarting(
                            Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                    [state = state, weakOwner = owner->weak_from_this()](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                                        Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> operation;
                                        if (FAILED(args->get_DownloadOperation(operation.GetAddressOf())) || !operation) {
                                            args->put_Cancel(TRUE);
                                            return S_OK;
                                        }
                                        LPWSTR rawUri = nullptr;
                                        operation->get_Uri(&rawUri);
                                        const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                        CoTaskMemFree(rawUri);
                                        LPWSTR rawPath = nullptr;
                                        args->get_ResultFilePath(&rawPath);
                                        const QString suggestedName = QFileInfo(QString::fromWCharArray(rawPath ? rawPath : L"")).fileName();
                                        CoTaskMemFree(rawPath);
                                        const DownloadRequest request { url, normalizedOrigin(state->committedUrl), state->committedUrl, suggestedName };
                                        if (!state->policy || state->policy->decideDownload(request) != DownloadDecision::Allow
                                            || !state->callbacks.resolveDownload) {
                                            args->put_Cancel(TRUE);
                                            return S_OK;
                                        }
                                        Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
                                        if (FAILED(args->GetDeferral(deferral.GetAddressOf())) || !deferral) {
                                            args->put_Cancel(TRUE);
                                            return S_OK;
                                        }
                                        auto guard = std::make_shared<HostCompletionGuard>(state);
                                        Microsoft::WRL::ComPtr<ICoreWebView2DownloadStartingEventArgs> argsRef(args);
                                        auto pending = std::make_shared<PendingAction>();
                                        pending->cancel = [argsRef, deferral] {
                                            argsRef->put_Cancel(TRUE);
                                            deferral->Complete();
                                        };
                                        if (const auto owner = weakOwner.lock()) owner->pendingActions.push_back(pending);
                                        state->callbacks.resolveDownload(request,
                                            [guard, argsRef, deferral, pending, weakOwner](DownloadResolution resolution) mutable {
                                                const auto access = guard->claim();
                                                if (access.claim == HostCompletionClaim::Duplicate) return;
                                                const auto owner = weakOwner.lock();
                                                if (!owner) return;
                                                owner->runOnUiThread(
                                                    [state = access.state, argsRef, deferral, resolution = std::move(resolution), pending, weakOwner]() mutable {
                                                        pending->finish([&] {
                                                            if (!state || state->lifetime.isClosed()) {
                                                                argsRef->put_Cancel(TRUE);
                                                            } else if (resolution.status != DownloadResolutionStatus::Resolved
                                                                || resolution.target.handling == DownloadHandling::Cancel) {
                                                                argsRef->put_Cancel(TRUE);
                                                            } else if (resolution.target.handling == DownloadHandling::TargetPath) {
                                                                const QFileInfo target(resolution.target.filePath);
                                                                if (!target.isAbsolute() || !target.dir().exists()) argsRef->put_Cancel(TRUE);
                                                                else argsRef->put_ResultFilePath(resolution.target.filePath.toStdWString().c_str());
                                                            }
                                                            deferral->Complete();
                                                        });
                                                        if (const auto owner = weakOwner.lock()) owner->removePendingAction(pending);
                                                    });
                                            });
                                        return S_OK;
                                    }).Get(), &downloadToken);
                        }
                        webview->add_NavigationStarting(
                            Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [state = state](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR rawUri = nullptr;
                                    BOOL redirected = FALSE;
                                    BOOL userInitiated = FALSE;
                                    UINT64 webviewNavigationId = 0;
                                    args->get_Uri(&rawUri);
                                    args->get_IsRedirected(&redirected);
                                    args->get_IsUserInitiated(&userInitiated);
                                    args->get_NavigationId(&webviewNavigationId);
                                    const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                    CoTaskMemFree(rawUri);
                                    const quint64 id = state->navigationId = webviewNavigationId;
                                    state->documentToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                    const auto decision = state->policy ? state->policy->decideNavigation({ url, true, userInitiated != FALSE, redirected != FALSE }) : NavigationDecision::Allow;
                                    if (decision == NavigationDecision::Cancel) {
                                        args->put_Cancel(TRUE);
                                        state->lifetime.invalidate();
                                        state->emitLoad(LoadState::Failed, id, url, QStringLiteral("Navigation rejected by policy."));
                                        return S_OK;
                                    }
                                    if (decision == NavigationDecision::OpenExternally) {
                                        args->put_Cancel(TRUE);
                                        if (state->callbacks.openExternal) state->callbacks.openExternal(url);
                                        return S_OK;
                                    }
                                    state->lifetime.invalidate();
                                    state->emitLoad(redirected ? LoadState::Redirected : LoadState::Started, id, url);
                                    return S_OK;
                                }).Get(), &navigationStartingToken);
                        webview->add_SourceChanged(
                            Microsoft::WRL::Callback<ICoreWebView2SourceChangedEventHandler>(
                                [state = state](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                                    LPWSTR rawSource = nullptr;
                                    if (SUCCEEDED(sender->get_Source(&rawSource))) {
                                        state->committedUrl = QUrl(QString::fromWCharArray(rawSource ? rawSource : L""));
                                    }
                                    CoTaskMemFree(rawSource);
                                    return S_OK;
                                }).Get(), &sourceChangedToken);
                        webview->add_ContentLoading(
                            Microsoft::WRL::Callback<ICoreWebView2ContentLoadingEventHandler>(
                                [state = state](ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
                                    UINT64 id = 0;
                                    LPWSTR rawSource = nullptr;
                                    args->get_NavigationId(&id);
                                    if (SUCCEEDED(sender->get_Source(&rawSource))) {
                                        state->committedUrl = QUrl(QString::fromWCharArray(rawSource ? rawSource : L""));
                                    }
                                    CoTaskMemFree(rawSource);
                                    const QString tokenLiteral = QString::fromUtf8(QJsonDocument(QJsonArray { state->documentToken }).toJson(QJsonDocument::Compact));
                                    const QString tokenScript = QStringLiteral("window.__systemWebViewToken=%1[0];").arg(tokenLiteral);
                                    sender->ExecuteScript(tokenScript.toStdWString().c_str(),
                                        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                            [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
                                    state->emitLoad(LoadState::Committed, id, state->committedUrl);
                                    return S_OK;
                                }).Get(), &contentLoadingToken);
                        webview->add_NavigationCompleted(
                            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [state = state](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL success = FALSE;
                                    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                    UINT64 id = 0;
                                    args->get_IsSuccess(&success);
                                    args->get_WebErrorStatus(&status);
                                    args->get_NavigationId(&id);
                                    LPWSTR rawUri = nullptr;
                                    sender->get_Source(&rawUri);
                                    const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                    CoTaskMemFree(rawUri);
                                    if (success) {
                                        state->committedUrl = url;
                                        state->emitLoad(LoadState::Finished, id, url);
                                    } else {
                                        state->emitLoad(LoadState::Failed, id, url, QStringLiteral("WebView2 navigation failed (status %1).").arg(static_cast<int>(status)));
                                    }
                                    return S_OK;
                                }).Get(), &navigationCompletedToken);
                        webview->add_WebMessageReceived(
                            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [state = state](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    // This SDK event exposes source origin but no main-frame flag. The
                                    // top-level document token is the additional document boundary.
                                    LPWSTR rawJson = nullptr;
                                    LPWSTR rawSource = nullptr;
                                    args->get_WebMessageAsJson(&rawJson);
                                    args->get_Source(&rawSource);
                                    const QString json = QString::fromWCharArray(rawJson ? rawJson : L"");
                                    const QUrl source = QUrl(QString::fromWCharArray(rawSource ? rawSource : L""));
                                    CoTaskMemFree(rawJson);
                                    CoTaskMemFree(rawSource);
                                    if (json.toUtf8().size() > 1024 * 1024 || state->committedUrl.isEmpty()) return S_OK;
                                    QJsonObject object;
                                    QString error;
                                    if (!state->policy || !state->policy->allowsBridge(source)
                                        || source.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)
                                            != state->committedUrl.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)
                                        || !parseMessage(json, &object, &error)
                                        || object.value(QStringLiteral("documentToken")).toString() != state->documentToken) return S_OK;
                                    object = object.value(QStringLiteral("message")).toObject();
                                    BridgeMessage message;
                                    message.version = object.value(QStringLiteral("version")).toInt();
                                    message.type = object.value(QStringLiteral("type")).toString();
                                    message.payload = object.value(QStringLiteral("payload")).toObject();
                                    if (!state->policy->validateBridgeMessage(message, &error)) return S_OK;
                                    if (state->callbacks.message) state->callbacks.message(message);
                                    return S_OK;
                                }).Get(), &webMessageToken);
                        const QString transport = QStringLiteral(R"JS((() => {
  window.__systemWebViewToken = null;
  const transport = Object.freeze({ postMessage(message) {
    window.chrome.webview.postMessage({ documentToken: window.__systemWebViewToken, message });
  }});
  Object.defineProperty(window, 'systemWebView', { value: transport, configurable: false, enumerable: true, writable: false });
  window.chrome.webview.addEventListener('message', function(event) {
    const message = event.data;
    window.dispatchEvent(new CustomEvent('system-webview-message', { detail: message }));
  });
})();)JS");
                        const HRESULT transportResult = webview->AddScriptToExecuteOnDocumentCreated(
                            transport.toStdWString().c_str(),
                            Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                [state, owner](HRESULT result, LPCWSTR) -> HRESULT {
                                    if (FAILED(result)) {
                                        state->failInitialization(QStringLiteral("WebView2 document transport registration failed (HRESULT 0x%1).")
                                            .arg(QString::number(static_cast<quint32>(result), 16)));
                                        return S_OK;
                                    }
                                    state->documentTransportPrepared = true;
                                    state->markReady();
                                    owner->applyHostState();
                                    return S_OK;
                                }).Get());
                        if (FAILED(transportResult)) {
                            state->failInitialization(QStringLiteral("WebView2 document transport request failed (HRESULT 0x%1).")
                                .arg(QString::number(static_cast<quint32>(transportResult), 16)));
                        }
                        return S_OK;
                    });
            HRESULT createResult = E_NOINTERFACE;
            if (owner->sessionMode == SessionMode::Ephemeral) {
                Microsoft::WRL::ComPtr<ICoreWebView2Environment10> environment10;
                Microsoft::WRL::ComPtr<ICoreWebView2ControllerOptions> controllerOptions;
                if (SUCCEEDED(owner->environment.As(&environment10)) && environment10
                    && SUCCEEDED(environment10->CreateCoreWebView2ControllerOptions(&controllerOptions))
                    && controllerOptions
                    && SUCCEEDED(controllerOptions->put_IsInPrivateModeEnabled(TRUE))) {
                    createResult = environment10->CreateCoreWebView2ControllerWithOptions(
                        hwnd, controllerOptions.Get(), completed.Get());
                }
            } else {
                createResult = owner->environment->CreateCoreWebView2Controller(hwnd, completed.Get());
            }
            if (FAILED(createResult)) {
                const QString operation = owner->sessionMode == SessionMode::Ephemeral
                    ? QStringLiteral("InPrivate controller")
                    : QStringLiteral("controller");
                owner->state->failInitialization(QStringLiteral("WebView2 %1 request failed (HRESULT 0x%2).").arg(operation, QString::number(static_cast<quint32>(createResult), 16)));
            }
        });
    }

    NativeViewHost* container;
    std::shared_ptr<WebViewState> state;
    std::shared_ptr<WebViewState> sessionState;
    std::function<ICoreWebView2Environment*()> environmentProvider;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    EventRegistrationToken navigationStartingToken{};
    EventRegistrationToken sourceChangedToken{};
    EventRegistrationToken contentLoadingToken{};
    EventRegistrationToken navigationCompletedToken{};
    EventRegistrationToken webMessageToken{};
    EventRegistrationToken webResourceToken{};
    EventRegistrationToken permissionToken{};
    EventRegistrationToken newWindowToken{};
    EventRegistrationToken downloadToken{};
    WebViewPolicyPtr policy;
    QVector<WebResourceMapping> resourceMappings;
    SessionMode sessionMode = SessionMode::Persistent;
    std::function<QString(ICoreWebView2*)> registerProfile;
    std::function<void(std::function<void()>)> registerSessionClose;
    std::shared_ptr<QHash<QString, QByteArray>> inlineDocuments;
    std::vector<std::shared_ptr<PendingAction>> pendingActions;
    bool attached = false;
    bool eventsRemoved = false;
    bool ownsContainer = false;
    QThread* uiThread = QThread::currentThread();

    void removePendingAction(const std::shared_ptr<PendingAction>& pending)
    {
        pendingActions.erase(std::remove(pendingActions.begin(), pendingActions.end(), pending), pendingActions.end());
    }

    void cancelPendingActions()
    {
        auto pending = std::move(pendingActions);
        pendingActions.clear();
        for (const auto& action : pending) action->finish(action->cancel);
    }

    void close()
    {
        if (state->lifetime.isClosed()) return;
        attached = false;
        cancelPendingActions();
        removeEvents();
        if (webview) webview->Stop();
        if (controller) {
            controller->put_IsVisible(FALSE);
            controller->Close();
            controller.Reset();
            webview.Reset();
        }
        state->callbacks = {};
        state->close();
    }

    bool runOnUiThread(std::function<void()> action)
    {
        if (!action || !container) return false;
        if (QThread::currentThread() == uiThread) {
            action();
            return true;
        }
        return QMetaObject::invokeMethod(container, std::move(action), Qt::QueuedConnection);
    }

    void applyHostState()
    {
        if (!controller) return;
        if (!attached) {
            controller->put_IsVisible(FALSE);
            return;
        }
        const HWND hwnd = reinterpret_cast<HWND>(container->winId());
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        controller->put_ParentWindow(hwnd);
        controller->put_Bounds(bounds);
        controller->put_IsVisible(TRUE);
    }

    void removeEvents()
    {
        if (!eventsRemoved && webview) {
            eventsRemoved = true;
            webview->remove_NavigationStarting(navigationStartingToken);
            webview->remove_SourceChanged(sourceChangedToken);
            webview->remove_ContentLoading(contentLoadingToken);
            webview->remove_NavigationCompleted(navigationCompletedToken);
            webview->remove_WebMessageReceived(webMessageToken);
            webview->remove_WebResourceRequested(webResourceToken);
            webview->remove_PermissionRequested(permissionToken);
            webview->remove_NewWindowRequested(newWindowToken);
            if (webview) {
                Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
                if (SUCCEEDED(webview.As(&webview4)) && webview4) webview4->remove_DownloadStarting(downloadToken);
            }
        }
    }

    ~Impl()
    {
        cancelPendingActions();
        removeEvents();
        if (ownsContainer) delete container;
    }
};

WebView2View::WebView2View(QWidget* parent, std::function<ICoreWebView2Environment*()> environmentProvider,
    std::shared_ptr<WebViewState> sessionState, WebViewPolicyPtr policy,
    QVector<WebResourceMapping> resourceMappings, SessionMode sessionMode,
    std::function<QString(ICoreWebView2*)> registerProfile,
    std::function<void(std::function<void()>)> registerSessionClose)
    : impl_(std::make_shared<Impl>(parent, std::move(environmentProvider), std::move(sessionState), std::move(policy),
          std::move(resourceMappings), sessionMode, std::move(registerProfile), std::move(registerSessionClose)))
{
    impl_->start();
}

WebView2View::~WebView2View() { close(); }
QWidget* WebView2View::widget() { return impl_->container; }
InitializationState WebView2View::initializationState() const { return impl_->state->initializationState(); }
void WebView2View::whenInitialized(InitializationCompletion completion) { impl_->state->whenInitialized(std::move(completion)); }
void WebView2View::attachNativeView()
{
    impl_->attached = true;
    impl_->applyHostState();
}
void WebView2View::detachNativeView()
{
    impl_->attached = false;
    impl_->cancelPendingActions();
    impl_->applyHostState();
}

void WebView2View::load(const QUrl& url)
{
    impl_->state->runWhenReady([state = impl_->state, url, owner = std::weak_ptr<Impl>(impl_)](const InitializationResult& result) {
        if (result.state != InitializationState::Ready) {
            state->emitLoad(LoadState::Failed, ++state->navigationId, url, result.error);
            return;
        }
        const auto view = owner.lock();
        const auto webview = view ? view->webview : nullptr;
        const auto text = url.toString().toStdWString();
        if (!webview) {
            state->emitLoad(LoadState::Failed, ++state->navigationId, url,
                QStringLiteral("WebView2 core object is unavailable."));
            return;
        }
        const HRESULT navigateResult = webview->Navigate(text.c_str());
        if (FAILED(navigateResult)) {
            state->emitLoad(LoadState::Failed, ++state->navigationId, url,
                QStringLiteral("WebView2 navigation request failed (HRESULT 0x%1).")
                    .arg(QString::number(static_cast<quint32>(navigateResult), 16)));
        }
    });
}

void WebView2View::setHtml(const QString& html, const QUrl& baseUrl)
{
    if (baseUrl.scheme() == QStringLiteral("app") && !findResourceMapping(impl_->resourceMappings, baseUrl)) {
        const auto id = ++impl_->state->navigationId;
        impl_->state->emitLoad(LoadState::Failed, id, baseUrl, QStringLiteral("ResourceMapping capability is unsupported for this app origin."));
        return;
    }
    impl_->inlineDocuments->insert(baseUrl.toString(QUrl::FullyEncoded), html.toUtf8());
    load(baseUrl);
}
void WebView2View::stop() { if (impl_->webview) impl_->webview->Stop(); }
void WebView2View::reload() { if (impl_->webview) impl_->webview->Reload(); }

void WebView2View::close()
{
    if (impl_) impl_->close();
}

bool WebView2View::isClosed() const { return impl_->state->lifetime.isClosed(); }

void WebView2View::sendMessage(const BridgeMessage& message, MessageCompletion completion)
{
    if (!impl_->webview) { if (completion) completion({ MessageError::Closed, QStringLiteral("WebView2 controller is unavailable.") }); return; }
    const QJsonObject object { { QStringLiteral("version"), message.version }, { QStringLiteral("type"), message.type }, { QStringLiteral("payload"), message.payload } };
    const auto json = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)).toStdWString();
    const HRESULT hr = impl_->webview->PostWebMessageAsJson(json.c_str());
    if (completion) completion({ SUCCEEDED(hr) ? MessageError::None : MessageError::Rejected, SUCCEEDED(hr) ? QString() : QStringLiteral("WebView2 rejected the message.") });
}

void WebView2View::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    impl_->state->callbacks = std::move(callbacks);
}
} // namespace webview
