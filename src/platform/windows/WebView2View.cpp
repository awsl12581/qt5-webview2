#include "platform/windows/WebView2View.h"
#include "internal/Application.h"

#include "internal/BridgePageScript.h"
#include "internal/Diagnostics.h"
#include "internal/HttpStatus.h"
#include "internal/ResourceMapping.h"
#include "webview/HostCompletion.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewState.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QPointer>
#include <QResizeEvent>
#include <QThread>
#include <QUuid>
#include <QWidget>

#include <WebView2.h>
#include <shlwapi.h>
#include <windows.h>
#include <wrl.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace webview
{
namespace
{
RuntimeFailureEvent runtimeFailureEvent(COREWEBVIEW2_PROCESS_FAILED_KIND kind)
{
    switch (kind) {
    case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED:
        return { RuntimeFailureKind::BrowserProcessTerminated,
                 QStringLiteral("The WebView2 browser process terminated."),
                 static_cast<qint64>(kind) };
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE:
        return { RuntimeFailureKind::WebContentProcessUnresponsive,
                 QStringLiteral("The WebView2 render process is unresponsive."),
                 static_cast<qint64>(kind) };
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED:
        return { RuntimeFailureKind::WebContentProcessTerminated,
                 QStringLiteral("A WebView2 render process terminated."),
                 static_cast<qint64>(kind) };
    case COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_SANDBOX_HELPER_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_PLUGIN_PROCESS_EXITED:
    case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_BROKER_PROCESS_EXITED:
        return { RuntimeFailureKind::AuxiliaryProcessTerminated,
                 QStringLiteral("A WebView2 auxiliary process terminated."),
                 static_cast<qint64>(kind) };
    case COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED:
        break;
    }
    return { RuntimeFailureKind::Unknown, QStringLiteral("An unknown WebView2 process terminated."), static_cast<qint64>(kind) };
}

class WebView2BridgeTransport final : public BridgeTransport
{
public:
    explicit WebView2BridgeTransport(Microsoft::WRL::ComPtr<ICoreWebView2> view)
        : view_(std::move(view))
    {
    }

    bool send(const QByteArray& message) override
    {
        if (!view_) {
            return false;
        }
        const auto text = QString::fromUtf8(message).toStdWString();
        return SUCCEEDED(view_->PostWebMessageAsJson(text.c_str()));
    }

    void invalidate() override { view_.Reset(); }

private:
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
};

class FileRangeStream final : public IStream
{
public:
    FileRangeStream(const QString& path, qint64 offset, qint64 length, std::shared_ptr<void> lease)
        : offset_(offset)
        , length_(length)
        , lease_(std::move(lease))
    {
        file_ = CreateFileW(
            path.toStdWString().c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (file_ != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER position;
            position.QuadPart = offset_;
            SetFilePointerEx(file_, position, nullptr, FILE_BEGIN);
        }
    }

    ~FileRangeStream()
    {
        if (file_ != INVALID_HANDLE_VALUE) {
            CloseHandle(file_);
        }
    }

    bool valid() const { return file_ != INVALID_HANDLE_VALUE; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object) {
            return E_POINTER;
        }
        if (iid == IID_IUnknown || iid == IID_ISequentialStream || iid == IID_IStream) {
            *object = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = --references_;
        if (!count) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG bytes, ULONG* read) override
    {
        if (!buffer) {
            return E_POINTER;
        }
        const auto remaining = length_ - position_;
        const DWORD amount = static_cast<DWORD>(qMin<qint64>(bytes, qMax<qint64>(0, remaining)));
        DWORD actual = 0;
        if (amount && !ReadFile(file_, buffer, amount, &actual, nullptr)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        position_ += actual;
        if (read) {
            *read = actual;
        }
        return actual == bytes ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* result) override
    {
        qint64 base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? position_ : origin == STREAM_SEEK_END ? length_ : -1;
        if (base < 0) {
            return STG_E_INVALIDFUNCTION;
        }
        const qint64 next = base + move.QuadPart;
        if (next < 0 || next > length_) {
            return STG_E_INVALIDFUNCTION;
        }
        LARGE_INTEGER absolute;
        absolute.QuadPart = offset_ + next;
        if (!SetFilePointerEx(file_, absolute, nullptr, FILE_BEGIN)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        position_ = next;
        if (result) {
            result->QuadPart = static_cast<ULONGLONG>(position_);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* target, ULARGE_INTEGER count, ULARGE_INTEGER* read, ULARGE_INTEGER* written) override
    {
        if (!target) {
            return E_POINTER;
        }
        ULONGLONG totalRead = 0, totalWritten = 0;
        BYTE buffer[64 * 1024];
        while (totalRead < count.QuadPart) {
            ULONG got = 0;
            const ULONG request = static_cast<ULONG>(qMin<ULONGLONG>(sizeof(buffer), count.QuadPart - totalRead));
            const HRESULT result = Read(buffer, request, &got);
            if (FAILED(result) || !got) {
                break;
            }
            ULONG sent = 0;
            const HRESULT writeResult = target->Write(buffer, got, &sent);
            totalRead += got;
            totalWritten += sent;
            if (FAILED(writeResult) || sent != got) {
                break;
            }
        }
        if (read) {
            read->QuadPart = totalRead;
        }
        if (written) {
            written->QuadPart = totalWritten;
        }
        return totalRead == count.QuadPart ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_REVERTED; }

    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }

    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override
    {
        if (!stat) {
            return E_POINTER;
        }
        ZeroMemory(stat, sizeof(*stat));
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = static_cast<ULONGLONG>(length_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

private:
    std::atomic<ULONG> references_ { 1 };
    HANDLE file_ = INVALID_HANDLE_VALUE;
    qint64 offset_ = 0;
    qint64 length_ = 0;
    qint64 position_ = 0;
    std::shared_ptr<void> lease_;
};

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
        if (resized) {
            resized();
        }
    }
};
}

class WebView2View::Impl : public std::enable_shared_from_this<WebView2View::Impl>
{
public:
    struct PendingAction
    {
        std::atomic_bool completed = false;
        std::function<void()> cancel;

        bool finish(const std::function<void()>& action)
        {
            if (completed.exchange(true)) {
                return false;
            }
            if (action) {
                action();
            }
            return true;
        }
    };

    explicit Impl(
        QWidget* parent,
        std::function<ICoreWebView2Environment*()> environmentProvider,
        std::shared_ptr<WebViewState> sessionState,
        WebViewPolicyPtr policy,
        std::shared_ptr<QVector<ResourceMapping>> resourceMappings,
        SessionMode sessionMode,
        std::function<QString(ICoreWebView2*)> registerProfile,
        std::function<void(std::function<void()>)> registerSessionClose)
        : container(new NativeViewHost(parent))
        , state(std::make_shared<WebViewState>(DiagnosticScope::View, sessionState ? sessionState->diagnosticId : 0))
        , sessionState(std::move(sessionState))
        , environmentProvider(std::move(environmentProvider))
        , policy(std::move(policy))
        , resourceMappings(std::move(resourceMappings))
        , sessionMode(sessionMode)
        , registerProfile(std::move(registerProfile))
        , registerSessionClose(std::move(registerSessionClose))
        , inlineDocuments(std::make_shared<QHash<QString, QByteArray>>())
        , ownsContainer(parent == nullptr)
    {
        state->policy = this->policy;
        state->bindBridgePolicy();
    }

    void start()
    {
        if (registerSessionClose) {
            registerSessionClose([weak = weak_from_this()] {
                if (const auto owner = weak.lock()) {
                    owner->close();
                }
            });
        }
        container->resized = [weak = weak_from_this()] {
            if (const auto owner = weak.lock()) {
                owner->applyHostState();
            }
        };
        sessionState->runWhenReady([weak = weak_from_this()](const InitializationResult& result) {
            const auto owner = weak.lock();
            if (!owner || owner->state->lifetime.isClosed()) {
                return;
            }
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
                    if (state->lifetime.isClosed()) {
                        return S_OK;
                    }
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
                    auto& processFailedToken = owner->processFailedToken;
                    if (FAILED(hr) || !created) {
                        state->failInitialization(QStringLiteral("WebView2 controller creation failed (HRESULT 0x%1).")
                                                      .arg(QString::number(static_cast<quint32>(hr), 16)));
                        return S_OK;
                    }
                    controller = created;
                    const HRESULT coreResult = controller->get_CoreWebView2(webview.GetAddressOf());
                    if (FAILED(coreResult) || !webview) {
                        state->failInitialization(QStringLiteral("WebView2 core object unavailable (HRESULT 0x%1).")
                                                      .arg(QString::number(static_cast<quint32>(coreResult), 16)));
                        return S_OK;
                    }
                    state->bridge->setTransport(std::make_unique<WebView2BridgeTransport>(webview));
                    const QString profileError = owner->registerProfile ? owner->registerProfile(webview.Get())
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
                            [state = state,
                             environment = environmentForRequests,
                             mappings = mappingsForRequests,
                             inlineDocuments =
                                 inlineDocumentsForRequests](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
                                if (FAILED(args->get_Request(request.GetAddressOf())) || !request) {
                                    return S_OK;
                                }
                                LPWSTR rawUri = nullptr;
                                request->get_Uri(&rawUri);
                                const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                CoTaskMemFree(rawUri);
                                QString mime = QStringLiteral("text/html");
                                int status = httpStatus::kOk;
                                qint64 totalSize = 0;
                                qint64 offset = 0;
                                qint64 length = 0;
                                std::shared_ptr<void> lease;
                                QString filePath;
                                const ResourceMapping* mapping = nullptr;
                                const auto inlineDocument = inlineDocuments->constFind(url.toString(QUrl::FullyEncoded));
                                if (inlineDocument != inlineDocuments->cend()) {
                                    const auto& bytes = inlineDocument.value();
                                    Microsoft::WRL::ComPtr<IStream> stream(SHCreateMemStream(
                                        reinterpret_cast<const BYTE*>(bytes.constData()),
                                        static_cast<UINT>(bytes.size())));
                                    if (!stream) {
                                        return S_OK;
                                    }
                                    const std::wstring headers =
                                        L"Content-Type: text/html\r\nContent-Length: " + std::to_wstring(bytes.size()) + L"\r\n";
                                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
                                    if (SUCCEEDED(environment->CreateWebResourceResponse(
                                            stream.Get(),
                                            httpStatus::kOk,
                                            L"OK",
                                            headers.c_str(),
                                            &response))
                                        && response) {
                                        args->put_Response(response.Get());
                                    }
                                    return S_OK;
                                }
                                ResourceResponse resourceResponse;
                                if (url.path().startsWith(QStringLiteral("/resource/"))) {
                                    QString rangeHeader;
                                    Microsoft::WRL::ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                                    LPWSTR rawRange = nullptr;
                                    if (SUCCEEDED(request->get_Headers(headers.GetAddressOf())) && headers
                                        && SUCCEEDED(headers->GetHeader(L"Range", &rawRange)) && rawRange) {
                                        rangeHeader = QString::fromWCharArray(rawRange);
                                        CoTaskMemFree(rawRange);
                                    }
                                    resourceResponse =
                                        state->resources->open({ url, state->committedUrl, state->documentToken, rangeHeader });
                                    status = resourceResponse.status;
                                    mime = resourceResponse.mimeType;
                                    totalSize = resourceResponse.totalSize;
                                    offset = resourceResponse.offset;
                                    length = resourceResponse.length;
                                    lease = std::move(resourceResponse.lease);
                                    auto* file = qobject_cast<QFile*>(resourceResponse.body.get());
                                    if (file) {
                                        filePath = file->fileName();
                                    }
                                    resourceResponse.body.reset();
                                }
                                else {
                                    mapping = findResourceMapping(*mappings, url);
                                    QString error;
                                    COREWEBVIEW2_WEB_RESOURCE_CONTEXT context { };
                                    args->get_ResourceContext(&context);
                                    filePath = mapping ? resolveMappedResource(
                                                             *mapping,
                                                             url,
                                                             &error,
                                                             context == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT)
                                                       : QString();
                                    if (filePath.isEmpty()) {
                                        return S_OK;
                                    }
                                    const QFileInfo info(filePath);
                                    mime = QMimeDatabase().mimeTypeForFile(filePath).name();
                                    totalSize = info.size();
                                    length = totalSize;
                                }
                                if (filePath.isEmpty()) {
                                    if (!url.path().startsWith(QStringLiteral("/resource/"))) {
                                        return S_OK;
                                    }
                                    Microsoft::WRL::ComPtr<IStream> empty(SHCreateMemStream(nullptr, 0));
                                    if (!empty) {
                                        return S_OK;
                                    }
                                    std::wstring errorHeaders =
                                        L"Content-Length: 0\r\nCache-Control: no-store\r\nContent-Disposition: inline\r\n";
                                    if (status == httpStatus::kRangeNotSatisfiable) {
                                        errorHeaders += L"Content-Range: bytes */" + std::to_wstring(totalSize) + L"\r\n";
                                    }
                                    const wchar_t* errorStatus = status == httpStatus::kForbidden             ? L"Forbidden"
                                                                 : status == httpStatus::kNotFound            ? L"Not Found"
                                                                 : status == httpStatus::kGone                ? L"Gone"
                                                                 : status == httpStatus::kRangeNotSatisfiable ? L"Range Not Satisfiable"
                                                                                                              : L"Read Error";
                                    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> errorResponse;
                                    if (SUCCEEDED(environment->CreateWebResourceResponse(
                                            empty.Get(),
                                            status,
                                            errorStatus,
                                            errorHeaders.c_str(),
                                            &errorResponse))
                                        && errorResponse) {
                                        args->put_Response(errorResponse.Get());
                                    }
                                    return S_OK;
                                }
                                Microsoft::WRL::ComPtr<IStream> stream;
                                auto* fileStream = new FileRangeStream(filePath, offset, length, std::move(lease));
                                if (!fileStream->valid()) {
                                    fileStream->Release();
                                    return S_OK;
                                }
                                stream.Attach(fileStream);
                                std::wstring headerText =
                                    L"Content-Type: " + mime.toStdWString() + L"\r\nContent-Length: " + std::to_wstring(length)
                                    + L"\r\nAccept-Ranges: bytes\r\nCache-Control: no-store\r\nContent-Disposition: inline\r\n";
                                if (mapping && mime == QStringLiteral("text/html")) {
                                    const auto policy = localBundleContentSecurityPolicy(mapping->externalNetworkAccess);
                                    if (!policy.isEmpty()) {
                                        headerText += L"Content-Security-Policy: " + policy.toStdWString() + L"\r\n";
                                    }
                                }
                                if (status == httpStatus::kPartialContent) {
                                    headerText += L"Content-Range: bytes " + std::to_wstring(offset) + L"-"
                                                  + std::to_wstring(offset + length - 1) + L"/" + std::to_wstring(totalSize) + L"\r\n";
                                }
                                const wchar_t* statusText = status == httpStatus::kPartialContent        ? L"Partial Content"
                                                            : status == httpStatus::kForbidden           ? L"Forbidden"
                                                            : status == httpStatus::kNotFound            ? L"Not Found"
                                                            : status == httpStatus::kGone                ? L"Gone"
                                                            : status == httpStatus::kRangeNotSatisfiable ? L"Range Not Satisfiable"
                                                            : status >= httpStatus::kInternalServerError ? L"Read Error"
                                                                                                         : L"OK";
                                Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
                                if (SUCCEEDED(
                                        environment
                                            ->CreateWebResourceResponse(stream.Get(), status, statusText, headerText.c_str(), &response))
                                    && response) {
                                    args->put_Response(response.Get());
                                }
                                return S_OK;
                            })
                            .Get(),
                        &webResourceToken);
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
                                if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE) {
                                    kind = PermissionKind::Microphone;
                                }
                                else if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_CAMERA) {
                                    kind = PermissionKind::Camera;
                                }
                                else if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION) {
                                    kind = PermissionKind::Location;
                                }
                                else if (nativeKind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ) {
                                    kind = PermissionKind::Clipboard;
                                }
                                const auto decision =
                                    state->policy ? state->policy->decidePermission({ kind, origin }) : PermissionDecision::Deny;
                                if (decision != PermissionDecision::Allow) {
                                    qCDebug(systemWebViewPolicy).noquote()
                                        << "event=policy.permission_denied" << "session=" << state->sessionDiagnosticId
                                        << "view=" << state->diagnosticId << "origin=" << diagnosticOrigin(origin);
                                }
                                args->put_State(
                                    decision == PermissionDecision::Allow ? COREWEBVIEW2_PERMISSION_STATE_ALLOW
                                                                          : COREWEBVIEW2_PERMISSION_STATE_DENY);
                                return S_OK;
                            })
                            .Get(),
                        &permissionToken);
                    webview->add_NewWindowRequested(
                        Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                            [state = state,
                             environment = environmentForRequests,
                             sessionState = sessionForChildren,
                             policy = policyForChildren,
                             mappings = mappingsForRequests,
                             sessionMode = sessionModeForChildren,
                             registerProfile = profileRegistrarForChildren,
                             registerSessionClose = sessionCloseRegistrarForChildren,
                             weakOwner =
                                 owner->weak_from_this()](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
                                args->GetDeferral(deferral.GetAddressOf());
                                LPWSTR rawUri = nullptr;
                                args->get_Uri(&rawUri);
                                const QUrl url = QUrl(QString::fromWCharArray(rawUri ? rawUri : L""));
                                CoTaskMemFree(rawUri);
                                BOOL userInitiated = FALSE;
                                args->get_IsUserInitiated(&userInitiated);
                                const NewWindowRequest request { url, userInitiated != FALSE };
                                const auto decision = state->policy ? state->policy->decideNewWindow(request) : NewWindowDecision::Cancel;
                                if (decision != NewWindowDecision::Allow || !state->callbacks.onNewWindow) {
                                    qCDebug(systemWebViewPolicy).noquote()
                                        << "event=policy.popup_denied" << "session=" << state->sessionDiagnosticId
                                        << "view=" << state->diagnosticId << "origin=" << diagnosticOrigin(url);
                                    args->put_Handled(TRUE);
                                    if (deferral) {
                                        deferral->Complete();
                                    }
                                    return S_OK;
                                }
                                auto childEnvironment = [environment]() -> ICoreWebView2Environment* {
                                    return environment.Get();
                                };
                                auto child = std::unique_ptr<IWebView>(new WebView2View(
                                    nullptr,
                                    std::move(childEnvironment),
                                    sessionState,
                                    policy,
                                    mappings,
                                    sessionMode,
                                    registerProfile,
                                    registerSessionClose));
                                auto* childView = static_cast<WebView2View*>(child.get());
                                auto childHolder = std::make_shared<WebViewPtr>(std::move(child));
                                Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs> argsRef(args);
                                auto pending = std::make_shared<PendingAction>();
                                pending->cancel = [argsRef, deferral, childHolder]() mutable {
                                    if (childHolder && *childHolder) {
                                        (*childHolder)->close();
                                    }
                                    if (childHolder) {
                                        childHolder->reset();
                                    }
                                    argsRef->put_Handled(TRUE);
                                    if (deferral) {
                                        deferral->Complete();
                                    }
                                };
                                if (const auto owner = weakOwner.lock()) {
                                    owner->pendingActions.push_back(pending);
                                }
                                childView->whenInitialized(
                                    [state, sessionState, request, childHolder, argsRef, deferral, pending, weakOwner](
                                        const InitializationResult& result) mutable {
                                        pending->finish([&] {
                                            if (state->lifetime.isClosed() || sessionState->lifetime.isClosed()) {
                                                if (childHolder && *childHolder) {
                                                    (*childHolder)->close();
                                                }
                                                if (childHolder) {
                                                    childHolder->reset();
                                                }
                                                argsRef->put_Handled(TRUE);
                                            }
                                            else if (result.state == InitializationState::Ready && childHolder && *childHolder) {
                                                auto* readyChild = static_cast<WebView2View*>(childHolder->get());
                                                argsRef->put_NewWindow(readyChild->impl_->webview.Get());
                                                argsRef->put_Handled(TRUE);
                                                if (state->callbacks.onNewWindow) {
                                                    state->callbacks.onNewWindow(request, std::move(*childHolder));
                                                }
                                            }
                                            else {
                                                if (childHolder && *childHolder) {
                                                    (*childHolder)->close();
                                                }
                                                if (childHolder) {
                                                    childHolder->reset();
                                                }
                                                argsRef->put_Handled(TRUE);
                                            }
                                            if (deferral) {
                                                deferral->Complete();
                                            }
                                        });
                                        if (const auto owner = weakOwner.lock()) {
                                            owner->removePendingAction(pending);
                                        }
                                        else if (!pending->completed) {
                                            argsRef->put_Handled(TRUE);
                                            if (deferral) {
                                                deferral->Complete();
                                            }
                                        }
                                    });
                                return S_OK;
                            })
                            .Get(),
                        &newWindowToken);
                    Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
                    if (SUCCEEDED(webview.As(&webview4)) && webview4) {
                        webview4->add_DownloadStarting(
                            Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                [state = state,
                                 weakOwner =
                                     owner->weak_from_this()](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
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
                                    const DownloadRequest request { url,
                                                                    normalizedOrigin(state->committedUrl),
                                                                    state->committedUrl,
                                                                    suggestedName };
                                    if (!state->policy || state->policy->decideDownload(request) != DownloadDecision::Allow
                                        || !state->callbacks.onResolveDownload) {
                                        qCDebug(systemWebViewPolicy).noquote()
                                            << "event=policy.download_denied" << "session=" << state->sessionDiagnosticId
                                            << "view=" << state->diagnosticId << "origin=" << diagnosticOrigin(url);
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
                                    if (const auto owner = weakOwner.lock()) {
                                        owner->pendingActions.push_back(pending);
                                    }
                                    state->callbacks.onResolveDownload(
                                        request,
                                        [guard, argsRef, deferral, pending, weakOwner](DownloadResolution resolution) mutable {
                                            const auto access = guard->claim();
                                            if (access.claim == HostCompletionClaim::Duplicate) {
                                                return;
                                            }
                                            const auto owner = weakOwner.lock();
                                            if (!owner) {
                                                return;
                                            }
                                            owner->runOnUiThread([state = access.state,
                                                                  argsRef,
                                                                  deferral,
                                                                  resolution = std::move(resolution),
                                                                  pending,
                                                                  weakOwner]() mutable {
                                                pending->finish([&] {
                                                    if (!state || state->lifetime.isClosed()) {
                                                        argsRef->put_Cancel(TRUE);
                                                    }
                                                    else if (
                                                        resolution.status != DownloadResolutionStatus::Resolved
                                                        || resolution.target.handling == DownloadHandling::Cancel) {
                                                        argsRef->put_Cancel(TRUE);
                                                    }
                                                    else if (resolution.target.handling == DownloadHandling::TargetPath) {
                                                        const QFileInfo target(resolution.target.filePath);
                                                        if (!target.isAbsolute() || !target.dir().exists()) {
                                                            argsRef->put_Cancel(TRUE);
                                                        }
                                                        else {
                                                            argsRef->put_ResultFilePath(resolution.target.filePath.toStdWString().c_str());
                                                        }
                                                    }
                                                    deferral->Complete();
                                                });
                                                if (const auto owner = weakOwner.lock()) {
                                                    owner->removePendingAction(pending);
                                                }
                                            });
                                        });
                                    return S_OK;
                                })
                                .Get(),
                            &downloadToken);
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
                                const auto decision =
                                    state->policy
                                        ? state->policy->decideNavigation({ url, true, userInitiated != FALSE, redirected != FALSE })
                                        : NavigationDecision::Allow;
                                if (decision == NavigationDecision::Cancel) {
                                    qCDebug(systemWebViewPolicy).noquote()
                                        << "event=navigation.rejected" << "session=" << state->sessionDiagnosticId
                                        << "view=" << state->diagnosticId << "origin=" << diagnosticOrigin(url);
                                    args->put_Cancel(TRUE);
                                    return S_OK;
                                }
                                if (decision == NavigationDecision::OpenExternally) {
                                    qCDebug(systemWebViewPolicy).noquote()
                                        << "event=navigation.rejected" << "session=" << state->sessionDiagnosticId
                                        << "view=" << state->diagnosticId << "origin=" << diagnosticOrigin(url);
                                    args->put_Cancel(TRUE);
                                    if (state->callbacks.onOpenExternal) {
                                        state->callbacks.onOpenExternal(url);
                                    }
                                    return S_OK;
                                }
                                if (!redirected) {
                                    state->invalidateDocument();
                                    state->documentToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                    state->setResourceDocumentToken(state->documentToken);
                                }
                                state->emitLoad(redirected ? LoadState::Redirected : LoadState::Started, id, url);
                                return S_OK;
                            })
                            .Get(),
                        &navigationStartingToken);
                    webview->add_SourceChanged(
                        Microsoft::WRL::Callback<ICoreWebView2SourceChangedEventHandler>(
                            [state = state](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                                LPWSTR rawSource = nullptr;
                                if (SUCCEEDED(sender->get_Source(&rawSource))) {
                                    state->committedUrl = QUrl(QString::fromWCharArray(rawSource ? rawSource : L""));
                                    state->setResourceContext(state->resourceOrigin, state->committedUrl, state->documentToken);
                                }
                                CoTaskMemFree(rawSource);
                                return S_OK;
                            })
                            .Get(),
                        &sourceChangedToken);
                    webview->add_ContentLoading(
                        Microsoft::WRL::Callback<ICoreWebView2ContentLoadingEventHandler>(
                            [state = state](ICoreWebView2* sender, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
                                UINT64 id = 0;
                                LPWSTR rawSource = nullptr;
                                args->get_NavigationId(&id);
                                if (SUCCEEDED(sender->get_Source(&rawSource))) {
                                    state->committedUrl = QUrl(QString::fromWCharArray(rawSource ? rawSource : L""));
                                    state->setResourceContext(state->resourceOrigin, state->committedUrl, state->documentToken);
                                }
                                CoTaskMemFree(rawSource);
                                const QString tokenLiteral =
                                    QString::fromUtf8(QJsonDocument(QJsonArray { state->documentToken }).toJson(QJsonDocument::Compact));
                                const QString tokenScript = QStringLiteral("window.__systemWebViewToken=%1[0];").arg(tokenLiteral);
                                sender->ExecuteScript(
                                    tokenScript.toStdWString().c_str(),
                                    Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>([](HRESULT, LPCWSTR) -> HRESULT {
                                        return S_OK;
                                    }).Get());
                                state->emitLoad(LoadState::Committed, id, state->committedUrl);
                                return S_OK;
                            })
                            .Get(),
                        &contentLoadingToken);
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
                                }
                                else {
                                    state->emitLoad(
                                        LoadState::Failed,
                                        id,
                                        url,
                                        QStringLiteral("WebView2 navigation failed (status %1).").arg(static_cast<int>(status)));
                                }
                                return S_OK;
                            })
                            .Get(),
                        &navigationCompletedToken);
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
                                if (state->committedUrl.isEmpty()) {
                                    qCDebug(systemWebViewBridge).noquote()
                                        << "event=bridge.message_rejected" << "session=" << state->sessionDiagnosticId
                                        << "view=" << state->diagnosticId << "reason=no_committed_document";
                                    return S_OK;
                                }
                                QJsonObject object;
                                QString error;
                                if (state->bridgeOrigin != normalizedOrigin(source) || !state->policy
                                    || !state->policy->allowsBridge(source)
                                    || source.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)
                                           != state->committedUrl.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment)
                                    || !parseMessage(json, &object, &error)
                                    || object.value(QStringLiteral("documentToken")).toString() != state->documentToken) {
                                    qCDebug(systemWebViewBridge).noquote()
                                        << "event=bridge.message_rejected" << "session=" << state->sessionDiagnosticId
                                        << "view=" << state->diagnosticId << "reason=native_boundary";
                                    return S_OK;
                                }
                                const auto inner = object.value(QStringLiteral("message")).toObject();
                                if (!inner.value(QStringLiteral("type")).isString() || !inner.value(QStringLiteral("payload")).isObject()) {
                                    return S_OK;
                                }
                                state->bridge->receive(QJsonDocument(inner).toJson(QJsonDocument::Compact), source);
                                return S_OK;
                            })
                            .Get(),
                        &webMessageToken);
                    webview->add_ProcessFailed(
                        Microsoft::WRL::Callback<ICoreWebView2ProcessFailedEventHandler>(
                            [state = state](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                                if (state->lifetime.isClosed() || !args) {
                                    return S_OK;
                                }
                                COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED;
                                if (FAILED(args->get_ProcessFailedKind(&kind))) {
                                    kind = COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED;
                                }
                                const auto event = runtimeFailureEvent(kind);
                                qCCritical(systemWebViewRuntime).noquote()
                                    << "event=runtime.process_failed" << "session=" << state->sessionDiagnosticId
                                    << "view=" << state->diagnosticId << "native_code=" << event.nativeCode;
                                state->emitRuntimeFailure(event);
                                return S_OK;
                            })
                            .Get(),
                        &processFailedToken);
                    const QString transport = bridgePageScript(
                        QStringLiteral("window.__systemWebViewToken"),
                        QStringLiteral("window.chrome.webview.postMessage"),
                        QStringLiteral(
                            "window.chrome.webview.addEventListener('message', event => window.__systemWebViewReceive(event.data));"),
                        QStringLiteral("window.__systemWebViewToken = null;"));
                    const HRESULT transportResult = webview->AddScriptToExecuteOnDocumentCreated(
                        transport.toStdWString().c_str(),
                        Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                            [state, owner](HRESULT result, LPCWSTR) -> HRESULT {
                                if (FAILED(result)) {
                                    state->failInitialization(
                                        QStringLiteral("WebView2 document transport registration failed (HRESULT 0x%1).")
                                            .arg(QString::number(static_cast<quint32>(result), 16)));
                                    return S_OK;
                                }
                                state->documentTransportPrepared = true;
                                state->markReady();
                                owner->applyHostState();
                                return S_OK;
                            })
                            .Get());
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
                    && SUCCEEDED(environment10->CreateCoreWebView2ControllerOptions(&controllerOptions)) && controllerOptions
                    && SUCCEEDED(controllerOptions->put_IsInPrivateModeEnabled(TRUE))) {
                    createResult = environment10->CreateCoreWebView2ControllerWithOptions(hwnd, controllerOptions.Get(), completed.Get());
                }
            }
            else {
                createResult = owner->environment->CreateCoreWebView2Controller(hwnd, completed.Get());
            }
            if (FAILED(createResult)) {
                const QString operation =
                    owner->sessionMode == SessionMode::Ephemeral ? QStringLiteral("InPrivate controller") : QStringLiteral("controller");
                owner->state->failInitialization(QStringLiteral("WebView2 %1 request failed (HRESULT 0x%2).")
                                                     .arg(operation, QString::number(static_cast<quint32>(createResult), 16)));
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
    EventRegistrationToken navigationStartingToken { };
    EventRegistrationToken sourceChangedToken { };
    EventRegistrationToken contentLoadingToken { };
    EventRegistrationToken navigationCompletedToken { };
    EventRegistrationToken webMessageToken { };
    EventRegistrationToken processFailedToken { };
    EventRegistrationToken webResourceToken { };
    EventRegistrationToken permissionToken { };
    EventRegistrationToken newWindowToken { };
    EventRegistrationToken downloadToken { };
    WebViewPolicyPtr policy;
    std::shared_ptr<QVector<ResourceMapping>> resourceMappings;
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
        for (const auto& action : pending) {
            action->finish(action->cancel);
        }
    }

    void close()
    {
        if (state->lifetime.isClosed()) {
            return;
        }
        attached = false;
        cancelPendingActions();
        removeEvents();
        if (webview) {
            webview->Stop();
        }
        if (controller) {
            controller->put_IsVisible(FALSE);
            controller->Close();
            controller.Reset();
            webview.Reset();
        }
        state->callbacks = { };
        state->close();
    }

    bool runOnUiThread(std::function<void()> action)
    {
        if (!action || !container) {
            return false;
        }
        if (QThread::currentThread() == uiThread) {
            action();
            return true;
        }
        return QMetaObject::invokeMethod(container, std::move(action), Qt::QueuedConnection);
    }

    void applyHostState()
    {
        if (!controller) {
            return;
        }
        if (!attached) {
            controller->put_IsVisible(FALSE);
            return;
        }
        const HWND hwnd = reinterpret_cast<HWND>(container->winId());
        RECT bounds { };
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
            webview->remove_ProcessFailed(processFailedToken);
            webview->remove_WebResourceRequested(webResourceToken);
            webview->remove_PermissionRequested(permissionToken);
            webview->remove_NewWindowRequested(newWindowToken);
            if (webview) {
                Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
                if (SUCCEEDED(webview.As(&webview4)) && webview4) {
                    webview4->remove_DownloadStarting(downloadToken);
                }
            }
        }
    }

    ~Impl()
    {
        cancelPendingActions();
        removeEvents();
        if (ownsContainer) {
            delete container;
        }
    }
};

WebView2View::WebView2View(
    QWidget* parent,
    std::function<ICoreWebView2Environment*()> environmentProvider,
    std::shared_ptr<WebViewState> sessionState,
    WebViewPolicyPtr policy,
    std::shared_ptr<QVector<ResourceMapping>> resourceMappings,
    SessionMode sessionMode,
    std::function<QString(ICoreWebView2*)> registerProfile,
    std::function<void(std::function<void()>)> registerSessionClose)
    : impl_(
          std::make_shared<Impl>(
              parent,
              std::move(environmentProvider),
              std::move(sessionState),
              std::move(policy),
              std::move(resourceMappings),
              sessionMode,
              std::move(registerProfile),
              std::move(registerSessionClose)))
{
    impl_->start();
}

WebView2View::~WebView2View()
{
    close();
}

QWidget* WebView2View::widget()
{
    return impl_->container;
}

InitializationState WebView2View::initializationState() const
{
    return impl_->state->initializationState();
}

void WebView2View::whenInitialized(InitializationCompletion completion)
{
    impl_->state->whenInitialized(std::move(completion));
}

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

void WebView2View::open(WebApplicationPtr application, const QString& route)
{
    if (!application) {
        return;
    }
    impl_->state->bridgeOrigin = application->bridgeAccess() == BridgeAccess::Allowed ? normalizedOrigin(application->origin()) : QUrl();
    impl_->state->resourceOrigin = QUrl(QStringLiteral("app://%1").arg(application->id()));
    impl_->state->setResourceContext(impl_->state->resourceOrigin, application->origin(), impl_->state->documentToken);
    navigate(application->urlForRoute(route));
}

void WebView2View::navigate(const QUrl& url)
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
            state->emitLoad(LoadState::Failed, ++state->navigationId, url, QStringLiteral("WebView2 core object is unavailable."));
            return;
        }
        const HRESULT navigateResult = webview->Navigate(text.c_str());
        if (FAILED(navigateResult)) {
            state->emitLoad(
                LoadState::Failed,
                ++state->navigationId,
                url,
                QStringLiteral("WebView2 navigation request failed (HRESULT 0x%1).")
                    .arg(QString::number(static_cast<quint32>(navigateResult), 16)));
        }
    });
}

void WebView2View::loadDocument(const QString& html, const QUrl& baseUrl)
{
    if (baseUrl.scheme() == QStringLiteral("app")
        && (!impl_->resourceMappings || !findResourceMapping(*impl_->resourceMappings, baseUrl))) {
        const auto id = ++impl_->state->navigationId;
        impl_->state->emitLoad(LoadState::Failed, id, baseUrl, QStringLiteral("The document base URL is unavailable."));
        return;
    }
    impl_->inlineDocuments->insert(baseUrl.toString(QUrl::FullyEncoded), html.toUtf8());
    navigate(baseUrl);
}

void WebView2View::stop()
{
    if (impl_->webview) {
        impl_->webview->Stop();
    }
}

void WebView2View::reload()
{
    if (impl_->webview) {
        impl_->state->invalidateDocument();
        impl_->state->committedUrl = QUrl();
        impl_->webview->Reload();
    }
}

void WebView2View::close()
{
    if (impl_) {
        impl_->close();
    }
}

bool WebView2View::isClosed() const
{
    return impl_->state->lifetime.isClosed();
}

WebViewBridge& WebView2View::bridge()
{
    return *impl_->state->bridge;
}

WebResourceManager& WebView2View::resources()
{
    return *impl_->state->resources;
}

void WebView2View::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    impl_->state->callbacks = std::move(callbacks);
}
} // namespace webview
