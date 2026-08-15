#include "platform/windows/WebView2View.h"

#include "webview/WebViewState.h"
#include "webview/JsonMessage.h"
#include "webview/ResourceMapping.h"

#include <QMetaObject>
#include <QJsonDocument>
#include <QFile>
#include <QMimeDatabase>
#include <QPointer>
#include <QWidget>

#include <WebView2.h>
#include <windows.h>
#include <wrl.h>
#include <shlwapi.h>

namespace webview
{
namespace {
class NativeViewHost final : public QWidget
{
public:
    explicit NativeViewHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
    }
};
}

class WebView2View::Impl
{
public:
    explicit Impl(QWidget* parent, ICoreWebView2Environment* environment,
        std::shared_ptr<WebViewState> sessionState, WebViewPolicyPtr policy,
        QVector<WebResourceMapping> resourceMappings)
        : container(new NativeViewHost(parent)), state(std::make_shared<WebViewState>()),
          sessionState(std::move(sessionState)), environment(environment), policy(std::move(policy)),
          resourceMappings(std::move(resourceMappings))
    {
        state->policy = this->policy;
        if (!environment) {
            state->failInitialization(QStringLiteral("WebView2 environment is unavailable."));
            return;
        }
        sessionState->runWhenReady([this, environment](const InitializationResult& result) {
            if (result.state != InitializationState::Ready) {
                state->failInitialization(result.error);
                return;
            }
            const HWND hwnd = reinterpret_cast<HWND>(container->winId());
            const HRESULT createResult = environment->CreateCoreWebView2Controller(hwnd,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [state = state, this](HRESULT hr, ICoreWebView2Controller* created) -> HRESULT {
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
                        const auto environmentForRequests = this->environment;
                        const auto mappingsForRequests = this->resourceMappings;
                        webview->AddWebResourceRequestedFilter(L"app://*/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                        webview->add_WebResourceRequested(
                            Microsoft::WRL::Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                [state = state, environment = environmentForRequests, mappings = mappingsForRequests](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
                                    if (FAILED(args->get_Request(request.GetAddressOf())) || !request) return S_OK;
                                    LPWSTR rawUri = nullptr;
                                    request->get_Uri(&rawUri);
                                    const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                    CoTaskMemFree(rawUri);
                                    const auto* mapping = findResourceMapping(mappings, url);
                                    QString error;
                                    const QString path = mapping ? resolveMappedResource(*mapping, url, &error) : QString();
                                    if (path.isEmpty()) return S_OK;
                                    QFile file(path);
                                    if (!file.open(QIODevice::ReadOnly)) return S_OK;
                                    const QByteArray bytes = file.readAll();
                                    Microsoft::WRL::ComPtr<IStream> stream(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.constData()), static_cast<UINT>(bytes.size())));
                                    if (!stream) return S_OK;
                                    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
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
                                    const QUrl origin = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
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
                                [state = state](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    LPWSTR rawUri = nullptr;
                                    args->get_Uri(&rawUri);
                                    const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                    CoTaskMemFree(rawUri);
                                    const NewWindowRequest request { url, false };
                                    const auto decision = state->policy ? state->policy->decideNewWindow(request) : NewWindowDecision::Cancel;
                                    // Child ownership must be transferred by a host callback; without one the deferral is rejected.
                                    args->put_Handled(TRUE);
                                    if (decision == NewWindowDecision::Allow && state->callbacks.newWindow) {
                                        state->callbacks.newWindow(request, {});
                                    }
                                    return S_OK;
                                }).Get(), &newWindowToken);
                        Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
                        if (SUCCEEDED(webview.As(&webview4)) && webview4) {
                            webview4->add_DownloadStarting(
                                Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                    [state = state](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                                        Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> operation;
                                        if (FAILED(args->get_DownloadOperation(operation.GetAddressOf())) || !operation) {
                                            args->put_Cancel(TRUE);
                                            return S_OK;
                                        }
                                        LPWSTR rawUri = nullptr;
                                        operation->get_Uri(&rawUri);
                                        const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                        CoTaskMemFree(rawUri);
                                        DownloadResolution resolution;
                                        if (state->callbacks.resolveDownload) {
                                            state->callbacks.resolveDownload({ url, {}, state->committedUrl, {} }, [&](DownloadResolution value) { resolution = std::move(value); });
                                        }
                                        if (resolution.status != DownloadResolutionStatus::Resolved || resolution.target.handling == DownloadHandling::Cancel) {
                                            args->put_Cancel(TRUE);
                                        } else if (resolution.target.handling == DownloadHandling::TargetPath) {
                                            args->put_ResultFilePath(resolution.target.filePath.toStdWString().c_str());
                                        }
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
                                    const auto decision = state->policy ? state->policy->decideNavigation({ url, true, userInitiated != FALSE, redirected != FALSE }) : NavigationDecision::Allow;
                                    if (decision == NavigationDecision::Cancel) args->put_Cancel(TRUE);
                                    if (decision == NavigationDecision::OpenExternally) {
                                        args->put_Cancel(TRUE);
                                        if (state->callbacks.openExternal) state->callbacks.openExternal(url);
                                    }
                                    state->emitLoad(decision == NavigationDecision::Cancel ? LoadState::Failed : (redirected ? LoadState::Redirected : LoadState::Started), id, url,
                                        decision == NavigationDecision::Cancel ? QStringLiteral("Navigation rejected by policy.") : QString());
                                    state->lifetime.invalidate();
                                    return S_OK;
                                }).Get(), &navigationStartingToken);
                        webview->add_NavigationCompleted(
                            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [state = state](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL success = FALSE;
                                    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                    UINT64 id = 0;
                                    args->get_IsSuccess(&success);
                                    args->get_WebErrorStatus(&status);
                                    args->get_NavigationId(&id);
                                    LPWSTR rawUri = nullptr;
                                    state->committedUrl = {};
                                    if (success) {
                                        state->emitLoad(LoadState::Finished, id, state->committedUrl);
                                    } else {
                                        state->emitLoad(LoadState::Failed, id, {}, QStringLiteral("WebView2 navigation failed (status %1).").arg(static_cast<int>(status)));
                                    }
                                    return S_OK;
                                }).Get(), &navigationCompletedToken);
                        webview->add_WebMessageReceived(
                            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [state = state](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    LPWSTR rawJson = nullptr;
                                    LPWSTR rawSource = nullptr;
                                    args->get_WebMessageAsJson(&rawJson);
                                    args->get_Source(&rawSource);
                                    const QString json = QString::fromWCharArray(rawJson ? rawJson : L"");
                                    const QUrl source = QUrl(QString::fromWCharArray(rawSource ? rawSource : L""));
                                    CoTaskMemFree(rawJson);
                                    CoTaskMemFree(rawSource);
                                    QJsonObject object;
                                    QString error;
                                    if (!state->policy || !state->policy->allowsBridge(source) || !parseMessage(json, &object, &error)) return S_OK;
                                    BridgeMessage message;
                                    message.version = object.value(QStringLiteral("version")).toInt();
                                    message.type = object.value(QStringLiteral("type")).toString();
                                    message.payload = object.value(QStringLiteral("payload")).toObject();
                                    if (!state->policy->validateBridgeMessage(message, &error)) return S_OK;
                                    if (state->callbacks.message) state->callbacks.message(message);
                                    return S_OK;
                                }).Get(), &webMessageToken);
                        state->markReady();
                        return S_OK;
                    }).Get());
            if (FAILED(createResult)) {
                state->failInitialization(QStringLiteral("WebView2 controller request failed (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(createResult), 16)));
            }
        });
    }

    NativeViewHost* container;
    std::shared_ptr<WebViewState> state;
    std::shared_ptr<WebViewState> sessionState;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    EventRegistrationToken navigationStartingToken{};
    EventRegistrationToken navigationCompletedToken{};
    EventRegistrationToken webMessageToken{};
    EventRegistrationToken webResourceToken{};
    EventRegistrationToken permissionToken{};
    EventRegistrationToken newWindowToken{};
    EventRegistrationToken downloadToken{};
    WebViewPolicyPtr policy;
    QVector<WebResourceMapping> resourceMappings;
    bool attached = false;

    ~Impl()
    {
        if (webview) {
            webview->remove_NavigationStarting(navigationStartingToken);
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
};

WebView2View::WebView2View(QWidget* parent, ICoreWebView2Environment* environment,
    std::shared_ptr<WebViewState> sessionState, WebViewPolicyPtr policy,
    QVector<WebResourceMapping> resourceMappings)
    : impl_(std::make_unique<Impl>(parent, environment, std::move(sessionState), std::move(policy), std::move(resourceMappings)))
{
}

WebView2View::~WebView2View() { close(); }
QWidget* WebView2View::widget() { return impl_->container; }
InitializationState WebView2View::initializationState() const { return impl_->state->initializationState(); }
void WebView2View::whenInitialized(InitializationCompletion completion) { impl_->state->whenInitialized(std::move(completion)); }
void WebView2View::attachNativeView()
{
    impl_->attached = true;
    if (impl_->controller) {
        RECT rect{};
        GetClientRect(reinterpret_cast<HWND>(impl_->container->winId()), &rect);
        impl_->controller->put_ParentWindow(reinterpret_cast<HWND>(impl_->container->winId()));
        impl_->controller->put_Bounds(rect);
        impl_->controller->put_IsVisible(TRUE);
    }
}
void WebView2View::detachNativeView()
{
    impl_->attached = false;
    if (impl_->controller) impl_->controller->put_IsVisible(FALSE);
}

void WebView2View::load(const QUrl& url)
{
    impl_->state->runWhenReady([state = impl_->state, url, webview = impl_->webview](const InitializationResult& result) {
        if (result.state != InitializationState::Ready) {
            state->emitLoad(LoadState::Failed, ++state->navigationId, url, result.error);
            return;
        }
        const auto text = url.toString().toStdWString();
        webview->Navigate(text.c_str());
        state->emitLoad(LoadState::Started, ++state->navigationId, url);
    });
}

void WebView2View::setHtml(const QString&, const QUrl& baseUrl)
{
    if (baseUrl.scheme() == QStringLiteral("app") && !findResourceMapping(impl_->resourceMappings, baseUrl)) {
        const auto id = ++impl_->state->navigationId;
        impl_->state->emitLoad(LoadState::Failed, id, baseUrl, QStringLiteral("ResourceMapping capability is unsupported for this app origin."));
        return;
    }
    load(baseUrl);
}
void WebView2View::stop() { if (impl_->webview) impl_->webview->Stop(); }
void WebView2View::reload() { if (impl_->webview) impl_->webview->Reload(); }

void WebView2View::close()
{
    if (!impl_ || impl_->state->lifetime.isClosed()) {
        return;
    }
    impl_->attached = false;
    if (impl_->controller) {
        impl_->controller->put_IsVisible(FALSE);
        impl_->controller->Close();
        impl_->controller.Reset();
        impl_->webview.Reset();
    }
    impl_->state->callbacks = {};
    impl_->state->close();
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
