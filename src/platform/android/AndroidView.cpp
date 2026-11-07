#include "platform/android/AndroidView.h"

#include "platform/android/AndroidJni.h"

#include "internal/Application.h"
#include "internal/BridgePageScript.h"
#include "internal/Diagnostics.h"
#include "internal/FileResource.h"
#include "internal/HttpStatus.h"
#include "internal/MimeType.h"
#include "internal/RandomToken.h"
#include "internal/ResourceMapping.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewState.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace webview
{
std::mutex registryMutex;
std::uint64_t nextViewId = 1;
std::unordered_map<std::uint64_t, std::weak_ptr<AndroidViewState>> viewRegistry;

std::uint64_t registerView(const std::shared_ptr<AndroidViewState>& view)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    const std::uint64_t id = nextViewId++;
    viewRegistry.insert_or_assign(id, std::weak_ptr<AndroidViewState>(view));
    return id;
}

void unregisterView(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    viewRegistry.erase(id);
}

std::shared_ptr<AndroidViewState> lookupView(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto found = viewRegistry.find(id);
    return found == viewRegistry.end() ? nullptr : found->second.lock();
}

namespace
{
constexpr auto kFeatureDocumentStart = "DOCUMENT_START_SCRIPT";
constexpr auto kFeatureMessageListener = "WEB_MESSAGE_LISTENER";
constexpr auto kFeatureMultiProfile = "MULTI_PROFILE";

void callVoid(JNIEnv* env, jobject target, jmethodID method)
{
    if (!env || !target || !method) {
        return;
    }
    env->CallVoidMethod(target, method);
    android::clearException(env);
}

void callVoidString(JNIEnv* env, jobject target, jmethodID method, const std::string& value)
{
    if (!env || !target || !method) {
        return;
    }
    jstring text = android::toJString(env, value);
    if (!text) {
        return;
    }
    env->CallVoidMethod(target, method, text);
    env->DeleteLocalRef(text);
    android::clearException(env);
}

bool callBoolean(JNIEnv* env, jobject target, jmethodID method)
{
    if (!env || !target || !method) {
        return false;
    }
    const jboolean value = env->CallBooleanMethod(target, method);
    android::clearException(env);
    return value == JNI_TRUE;
}

bool supportsFeature(JNIEnv* env, const android::NativeMethods& natives, const char* feature)
{
    jstring name = android::toJString(env, feature);
    if (!name) {
        return false;
    }
    const jboolean value = env->CallStaticBooleanMethod(natives.clazz, natives.supportsFeature, name);
    env->DeleteLocalRef(name);
    android::clearException(env);
    return value == JNI_TRUE;
}

class AndroidBridgeTransport final : public BridgeTransport
{
public:
    explicit AndroidBridgeTransport(std::weak_ptr<AndroidViewState> view)
        : view_(std::move(view))
    {
    }

    bool send(const std::string& message) override
    {
        const auto view = view_.lock();
        return view && view->postToPage(message);
    }

    void invalidate() override { view_.reset(); }

private:
    std::weak_ptr<AndroidViewState> view_;
};
} // namespace

AndroidViewState::AndroidViewState(
    JNIEnv* env,
    jobject hostView,
    std::shared_ptr<AndroidSessionState> session,
    SessionMode mode)
    : host_(hostView && env ? env->NewGlobalRef(hostView) : nullptr)
    , session_(std::move(session))
    , mode_(mode)
{
    const std::uint64_t sessionDiagnosticId = session_ && session_->scheduler ? session_->scheduler->diagnosticId : 0;
    state_ = std::make_shared<WebViewState>(DiagnosticScope::View, sessionDiagnosticId);
}

AndroidViewState::~AndroidViewState()
{
    close();
}

void AndroidViewState::start()
{
    if (!host_) {
        fail("The Android host view is unavailable.");
        return;
    }
    if (!session_ || session_->scheduler->initializationState() != InitializationState::Ready) {
        fail("The Android session is not ready.");
        return;
    }
    JNIEnv* current = android::env();
    if (!current) {
        fail("The Android JNI environment is unavailable.");
        return;
    }
    const auto* hosts = android::hostMethods(current);
    const auto* natives = android::nativeMethods(current);
    if (!hosts || !natives) {
        fail("The Android host adapter classes are unavailable.");
        return;
    }
    if (!current->CallStaticBooleanMethod(natives->clazz, natives->isMainThread)) {
        android::clearException(current);
        fail("The Android backend requires the Java main thread.");
        return;
    }
    id_ = registerView(shared_from_this());
    jstring originValue = static_cast<jstring>(current->CallObjectMethod(host_, hosts->assetOrigin));
    android::clearException(current);
    assetOrigin_ = android::toUtf8(current, originValue);
    if (originValue) {
        current->DeleteLocalRef(originValue);
    }
    if (assetOrigin_.empty()) {
        fail("The Android host adapter did not provide an asset origin.");
        return;
    }
    current->CallVoidMethod(host_, hosts->setNativeHandle, static_cast<jlong>(id_));
    android::clearException(current);
    if (!supportsFeature(current, *natives, kFeatureDocumentStart)
        || !supportsFeature(current, *natives, kFeatureMessageListener)) {
        fail("Android System WebView is missing the document-start or web-message features. Update Android System WebView.");
        return;
    }
    if (mode_ == SessionMode::Ephemeral) {
        if (!supportsFeature(current, *natives, kFeatureMultiProfile)) {
            fail("Ephemeral Android sessions require WebView MULTI_PROFILE support.");
            return;
        }
        callVoidString(current, host_, hosts->setProfileName, "systemwebview-" + std::to_string(id_));
    }
    if (session_) {
        session_->viewClosers.push_back([weak = weak_from_this()] {
            if (const auto view = weak.lock()) {
                view->close();
            }
        });
    }
    state_->policy = session_ ? session_->policy : createDefaultWebViewPolicy();
    state_->bindBridgePolicy();
    state_->bridge->setTransport(std::make_unique<AndroidBridgeTransport>(weak_from_this()));
    state_->markReady();
}

void AndroidViewState::fail(const std::string& error)
{
    if (!state_ || state_->lifetime.isClosed()) {
        return;
    }
    diagnosticLog(DiagnosticLevel::Warning, "system_webview.lifecycle")
        << "event=view.initialization_failed"
        << "session=" << (session_ && session_->scheduler ? session_->scheduler->diagnosticId : 0)
        << "view=" << (state_ ? state_->diagnosticId : 0)
        << "reason=" << (error.empty() ? "unknown" : error.substr(0, 96));
    state_->failInitialization(error);
}

void AndroidViewState::close()
{
    if (closed_) {
        return;
    }
    closed_ = true;
    unregisterView(id_);
    if (state_) {
        state_->callbacks = {};
        state_->documentToken.clear();
        state_->close();
    }
    if (JNIEnv* current = android::env()) {
        if (host_) {
            if (const auto* hosts = android::hostMethods(current)) {
                callVoid(current, host_, hosts->stopLoading);
                callVoid(current, host_, hosts->detach);
                callVoid(current, host_, hosts->destroy);
            }
            current->DeleteGlobalRef(host_);
        }
    }
    host_ = nullptr;
}

void AndroidViewState::attach()
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    if (JNIEnv* current = android::env()) {
        if (const auto* hosts = android::hostMethods(current)) {
            callVoid(current, host_, hosts->attach);
        }
    }
}

void AndroidViewState::detach()
{
    if (JNIEnv* current = android::env()) {
        if (host_) {
            if (const auto* hosts = android::hostMethods(current)) {
                callVoid(current, host_, hosts->detach);
            }
        }
    }
}

Url AndroidViewState::mapAssetUrl(const Url& assetUrl) const
{
    const std::string text = assetUrl.toString();
    if (assetOrigin_.empty()) {
        return assetUrl;
    }
    if (text == assetOrigin_ || text == assetOrigin_ + "/") {
        return state_->resourceOrigin;
    }
    const std::string prefix = assetOrigin_ + "/";
    if (text.compare(0, prefix.size(), prefix) != 0) {
        return assetUrl;
    }
    return Url("app://" + text.substr(prefix.size()));
}

std::string AndroidViewState::toAssetUrl(const Url& publicUrl) const
{
    if (assetOrigin_.empty()) {
        return {};
    }
    const std::string text = publicUrl.toString();
    if (publicUrl.scheme() != "app") {
        return text;
    }
    constexpr std::string_view prefix = "app://";
    if (text.size() <= prefix.size() || text.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    return assetOrigin_ + "/" + text.substr(prefix.size());
}

void AndroidViewState::installDocumentTransport()
{
    const auto tokenLiteral = serializeJson(state_->documentToken);
    if (!tokenLiteral) {
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.bridge")
            << "event=bridge.token_encoding_failed" << "session=" << state_->sessionDiagnosticId << "view=" << state_->diagnosticId;
        return;
    }
    const auto script = bridgePageScript(
        *tokenLiteral,
        // Android's web-message listener accepts strings or ArrayBuffers only.
        "(payload => window.systemWebViewNative.postMessage(JSON.stringify(payload)))",
        "window.systemWebViewNative.onmessage = event => window.__systemWebViewReceive(event.data);",
        "if (window.top !== window) return;");
    if (JNIEnv* current = android::env()) {
        if (const auto* hosts = android::hostMethods(current)) {
            callVoidString(current, host_, hosts->installDocumentScript, script);
        }
    }
}

void AndroidViewState::prepareDocument(const Url& publicUrl)
{
    if (!state_ || state_->lifetime.isClosed()) {
        return;
    }
    state_->navigationId += 1;
    failedNavigationId_ = 0;
    committedEmitted_ = false;
    state_->invalidateDocument();
    state_->documentToken = randomToken128();
    if (state_->documentToken.empty()) {
        state_->emitLoad(
            LoadState::Failed, state_->navigationId, publicUrl, "Secure document token generation failed.");
        return;
    }
    state_->setResourceDocumentToken(state_->documentToken);
    state_->committedUrl = publicUrl;
    state_->setResourceContext(state_->resourceOrigin, publicUrl, state_->documentToken);
    installDocumentTransport();
    prepared_ = true;
}

bool AndroidViewState::requireReady(const Url& url, const InitializationResult& result)
{
    if (result.state == InitializationState::Ready) {
        return true;
    }
    state_->emitLoad(LoadState::Failed, ++state_->navigationId, url, result.error);
    return false;
}

bool AndroidViewState::callLoadUrl(const std::string& assetUrl)
{
    JNIEnv* current = android::env();
    if (!current) {
        return false;
    }
    const auto* hosts = android::hostMethods(current);
    if (!hosts) {
        return false;
    }
    callVoidString(current, host_, hosts->loadUrl, assetUrl);
    return true;
}

void AndroidViewState::open(WebApplicationPtr application, const std::string& route)
{
    if (!application || closed_ || !state_) {
        return;
    }
    state_->bridgeOrigin = application->bridgeAccess() == BridgeAccess::Allowed ? normalizedOrigin(application->origin()) : Url();
    state_->resourceOrigin = Url("app://" + application->id());
    state_->setResourceContext(state_->resourceOrigin, application->origin(), state_->documentToken);
    navigate(application->urlForRoute(route));
}

void AndroidViewState::navigate(const Url& url)
{
    if (closed_ || !state_) {
        return;
    }
    auto self = shared_from_this();
    state_->runWhenReady([self, url](const InitializationResult& result) {
        if (!self->requireReady(url, result)) {
            return;
        }
        const auto target = self->toAssetUrl(url);
        if (target.empty()) {
            self->state_->emitLoad(LoadState::Failed, ++self->state_->navigationId, url, "The Android backend cannot load this URL.");
            return;
        }
        self->prepareDocument(url);
        if (self->state_->documentToken.empty()) {
            return;
        }
        self->callLoadUrl(target);
    });
}

void AndroidViewState::loadDocument(const std::string& html, const Url& baseUrl)
{
    if (closed_ || !state_) {
        return;
    }
    auto self = shared_from_this();
    state_->runWhenReady([self, html, baseUrl](const InitializationResult& result) {
        if (!self->requireReady(baseUrl, result)) {
            return;
        }
        if (baseUrl.scheme() == "app"
            && (!self->session_ || !findResourceMapping(*self->session_->resourceMappings, baseUrl))) {
            self->state_->emitLoad(
                LoadState::Failed,
                ++self->state_->navigationId,
                baseUrl,
                "The app origin has no configured resource mapping.");
            return;
        }
        const NavigationRequest request { baseUrl, true, false, false };
        if (!self->state_->policy || self->state_->policy->decideNavigation(request) != NavigationDecision::Allow) {
            self->state_->emitLoad(
                LoadState::Failed, ++self->state_->navigationId, baseUrl, "The HTML base URL was rejected by policy.");
            return;
        }
        const auto target = self->toAssetUrl(baseUrl);
        if (target.empty()) {
            self->state_->emitLoad(LoadState::Failed, ++self->state_->navigationId, baseUrl, "Invalid document base URL.");
            return;
        }
        self->prepareDocument(baseUrl);
        if (self->state_->documentToken.empty()) {
            return;
        }
        if (JNIEnv* current = android::env()) {
            if (const auto* hosts = android::hostMethods(current)) {
                jstring url = android::toJString(current, target);
                jstring body = android::toJString(current, html);
                if (url && body) {
                    current->CallVoidMethod(self->host_, hosts->loadDataWithBaseUrl, url, body);
                }
                if (url) {
                    current->DeleteLocalRef(url);
                }
                if (body) {
                    current->DeleteLocalRef(body);
                }
                android::clearException(current);
            }
        }
    });
}

void AndroidViewState::stop()
{
    if (closed_) {
        return;
    }
    if (JNIEnv* current = android::env()) {
        if (const auto* hosts = android::hostMethods(current)) {
            callVoid(current, host_, hosts->stopLoading);
        }
    }
}

void AndroidViewState::reload()
{
    if (closed_ || !state_ || state_->committedUrl.isEmpty()) {
        return;
    }
    prepareDocument(state_->committedUrl);
    if (JNIEnv* current = android::env()) {
        if (const auto* hosts = android::hostMethods(current)) {
            callVoid(current, host_, hosts->reloadPage);
        }
    }
}

bool AndroidViewState::canGoBack() const
{
    if (closed_) {
        return false;
    }
    JNIEnv* current = android::env();
    const auto* hosts = current ? android::hostMethods(current) : nullptr;
    return hosts && callBoolean(current, host_, hosts->canGoBack);
}

bool AndroidViewState::goBack()
{
    if (!canGoBack()) {
        return false;
    }
    if (JNIEnv* current = android::env()) {
        if (const auto* hosts = android::hostMethods(current)) {
            callVoid(current, host_, hosts->goBack);
        }
    }
    return true;
}

bool AndroidViewState::canGoForward() const
{
    if (closed_) {
        return false;
    }
    JNIEnv* current = android::env();
    const auto* hosts = current ? android::hostMethods(current) : nullptr;
    return hosts && callBoolean(current, host_, hosts->canGoForward);
}

bool AndroidViewState::goForward()
{
    if (!canGoForward()) {
        return false;
    }
    if (JNIEnv* current = android::env()) {
        if (const auto* hosts = android::hostMethods(current)) {
            callVoid(current, host_, hosts->goForward);
        }
    }
    return true;
}

void AndroidViewState::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    if (!closed_ && state_) {
        state_->callbacks = std::move(callbacks);
    }
}

WebViewBridge& AndroidViewState::bridge()
{
    return *state_->bridge;
}

WebResourceManager& AndroidViewState::resources()
{
    return *state_->resources;
}

void AndroidViewState::onPageStarted(const std::string& assetUrl)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    const Url url = mapAssetUrl(Url(assetUrl));
    if (!prepared_ || state_->committedUrl != url) {
        // Navigation that was not routed through the native policy (for example
        // a history entry) still receives a fresh document boundary.
        prepareDocument(url);
        if (state_->documentToken.empty()) {
            return;
        }
    }
    prepared_ = false;
    state_->emitLoad(LoadState::Started, state_->navigationId, url);
}

void AndroidViewState::onPageCommitVisible(const std::string& assetUrl)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    const Url url = mapAssetUrl(Url(assetUrl));
    state_->committedUrl = url;
    state_->setResourceContext(state_->resourceOrigin, url, state_->documentToken);
    if (committedEmitted_) {
        return;
    }
    committedEmitted_ = true;
    state_->emitLoad(LoadState::Committed, state_->navigationId, url);
}

void AndroidViewState::onPageFinished(const std::string& assetUrl)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    if (failedNavigationId_ == state_->navigationId) {
        return;
    }
    const Url url = mapAssetUrl(Url(assetUrl));
    state_->committedUrl = url;
    // Android can deliver onPageFinished before onPageCommitVisible. The portable
    // contract requires Committed to precede Finished, so emit it here if needed.
    if (!committedEmitted_) {
        committedEmitted_ = true;
        state_->setResourceContext(state_->resourceOrigin, url, state_->documentToken);
        state_->emitLoad(LoadState::Committed, state_->navigationId, url);
    }
    state_->emitLoad(LoadState::Finished, state_->navigationId, url);
}

void AndroidViewState::onReceivedError(const std::string& assetUrl, int nativeCode, const std::string& description)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    failedNavigationId_ = state_->navigationId;
    const Url url = mapAssetUrl(Url(assetUrl));
    const std::string error = description.empty() ? "Android WebView reported error " + std::to_string(nativeCode) : description;
    state_->emitLoad(LoadState::Failed, state_->navigationId, url, error);
}

int AndroidViewState::shouldOverrideUrlLoading(const std::string& assetUrl, bool mainFrame, bool userInitiated, bool redirect)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return 1;
    }
    const Url url = mapAssetUrl(Url(assetUrl));
    if (!url.isValid()) {
        return 1;
    }
    const auto decision = state_->policy
        ? state_->policy->decideNavigation({ url, mainFrame, userInitiated, redirect })
        : NavigationDecision::Cancel;
    if (decision == NavigationDecision::Cancel) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.policy")
            << "event=navigation.rejected" << "session=" << state_->sessionDiagnosticId << "view=" << state_->diagnosticId
            << "origin=" << diagnosticOrigin(url);
        return 1;
    }
    if (decision == NavigationDecision::OpenExternally) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.policy")
            << "event=navigation.rejected" << "session=" << state_->sessionDiagnosticId << "view=" << state_->diagnosticId
            << "origin=" << diagnosticOrigin(url);
        if (state_->callbacks.onOpenExternal) {
            state_->callbacks.onOpenExternal(url);
        }
        return 1;
    }
    if (mainFrame) {
        if (redirect) {
            state_->emitLoad(LoadState::Redirected, state_->navigationId, url);
        }
        else {
            prepareDocument(url);
        }
    }
    return 0;
}

void AndroidViewState::onMessage(const std::string& message, const std::string& sourceOrigin, bool mainFrame)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    const auto reject = [this](const char* reason) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.bridge")
            << "event=bridge.message_rejected" << "session=" << state_->sessionDiagnosticId << "view=" << state_->diagnosticId
            << "reason=" << reason;
    };
    if (!mainFrame) {
        reject("not_main_frame");
        return;
    }
    if (state_->committedUrl.isEmpty() || state_->bridgeOrigin.isEmpty() || !state_->policy) {
        reject("no_committed_document");
        return;
    }
    const Url source = mapAssetUrl(Url(sourceOrigin));
    if (source.origin() != state_->committedUrl.origin() || !state_->policy->allowsBridge(state_->committedUrl)
        || normalizedOrigin(source) != state_->bridgeOrigin) {
        reject("native_boundary");
        return;
    }
    Json wrapper;
    if (!parseMessage(message, &wrapper)) {
        reject("malformed_envelope");
        return;
    }
    if (jsonString(wrapper, "documentToken") != state_->documentToken) {
        reject("stale_document");
        return;
    }
    const auto inner = wrapper.find("message");
    if (inner == wrapper.end() || !inner->is_object()) {
        reject("malformed_envelope");
        return;
    }
    if (const auto bytes = serializeJson(*inner)) {
        state_->bridge->receive(*bytes, source);
    }
}

void AndroidViewState::onRenderProcessGone(int nativeCode)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return;
    }
    const RuntimeFailureEvent event {
        RuntimeFailureKind::WebContentProcessTerminated,
        "The Android WebView render process exited.",
        static_cast<std::int64_t>(nativeCode),
    };
    diagnosticLog(DiagnosticLevel::Critical, "system_webview.runtime")
        << "event=runtime.render_process_gone" << "session=" << state_->sessionDiagnosticId << "view=" << state_->diagnosticId
        << "native_code=" << nativeCode;
    state_->emitRuntimeFailure(event);
}

jobjectArray AndroidViewState::intercept(JNIEnv* env, const std::string& assetUrl, bool mainFrame)
{
    if (!env || closed_ || !state_ || state_->lifetime.isClosed() || !session_) {
        return nullptr;
    }
    const Url url = mapAssetUrl(Url(assetUrl));
    if (!url.isValid()) {
        return nullptr;
    }
    const auto makeResponse = [env](int status, const std::string& mime, std::vector<char>* body, const std::string& csp) {
        jclass objectClass = env->FindClass("java/lang/Object");
        jclass integerClass = env->FindClass("java/lang/Integer");
        if (!objectClass || !integerClass) {
            android::clearException(env);
            return static_cast<jobjectArray>(nullptr);
        }
        jobjectArray result = env->NewObjectArray(4, objectClass, nullptr);
        if (!result) {
            android::clearException(env);
            return static_cast<jobjectArray>(nullptr);
        }
        const jmethodID valueOf = env->GetStaticMethodID(integerClass, "valueOf", "(I)Ljava/lang/Integer;");
        jobject statusObject = valueOf ? env->CallStaticObjectMethod(integerClass, valueOf, status) : nullptr;
        android::clearException(env);
        if (statusObject) {
            env->SetObjectArrayElement(result, 0, statusObject);
            env->DeleteLocalRef(statusObject);
        }
        jstring mimeText = android::toJString(env, mime);
        if (mimeText) {
            env->SetObjectArrayElement(result, 1, mimeText);
            env->DeleteLocalRef(mimeText);
        }
        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(body ? body->size() : 0));
        if (bytes) {
            if (body && !body->empty()) {
                env->SetByteArrayRegion(
                    bytes, 0, static_cast<jsize>(body->size()), reinterpret_cast<const jbyte*>(body->data()));
            }
            env->SetObjectArrayElement(result, 2, bytes);
            env->DeleteLocalRef(bytes);
        }
        if (!csp.empty()) {
            jstring policy = android::toJString(env, csp);
            if (policy) {
                env->SetObjectArrayElement(result, 3, policy);
                env->DeleteLocalRef(policy);
            }
        }
        android::clearException(env);
        return result;
    };

    std::vector<char> bytes;
    if (url.path().starts_with("/resource/")) {
        ResourceRequest request;
        request.url = url;
        request.sourceOrigin = state_->committedUrl.origin();
        request.documentToken = state_->documentToken;
        const auto resource = state_->resources->open(request);
        if (resource.status != httpStatus::kOk && resource.status != httpStatus::kPartialContent) {
            return makeResponse(resource.status, resource.mimeType, &bytes, {});
        }
        if (resource.length > 0 && resource.body) {
            bytes.resize(static_cast<std::size_t>(resource.length));
            std::int64_t total = 0;
            while (total < resource.length) {
                const auto read = resource.body->read(bytes.data() + total, resource.length - total);
                if (read <= 0) {
                    break;
                }
                total += read;
            }
            bytes.resize(static_cast<std::size_t>(total));
        }
        return makeResponse(resource.status, resource.mimeType, &bytes, {});
    }

    const ResourceMapping* mapping = findResourceMapping(*session_->resourceMappings, url);
    if (!mapping) {
        return nullptr;
    }
    std::string error;
    const auto filePath = resolveMappedResource(*mapping, url, &error, mainFrame);
    if (filePath.empty()) {
        return makeResponse(httpStatus::kNotFound, "text/plain", &bytes, {});
    }
    std::error_code sizeError;
    const auto totalSize = std::filesystem::file_size(pathFromUtf8(filePath), sizeError);
    if (sizeError) {
        return makeResponse(httpStatus::kNotFound, "text/plain", &bytes, {});
    }
    std::ifstream file(pathFromUtf8(filePath), std::ios::binary);
    if (!file) {
        return makeResponse(httpStatus::kNotFound, "text/plain", &bytes, {});
    }
    bytes.resize(static_cast<std::size_t>(totalSize));
    if (!bytes.empty()) {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(file.gcount()));
    }
    const std::string mime(mime::fromPath(filePath));
    const std::string csp
        = mime == "text/html" ? localBundleContentSecurityPolicy(mapping->externalNetworkAccess) : std::string();
    return makeResponse(httpStatus::kOk, mime, &bytes, csp);
}

bool AndroidViewState::postToPage(const std::string& message)
{
    if (closed_ || !state_ || state_->lifetime.isClosed()) {
        return false;
    }
    JNIEnv* current = android::env();
    const auto* hosts = current ? android::hostMethods(current) : nullptr;
    if (!hosts) {
        return false;
    }
    jstring text = android::toJString(current, message);
    if (!text) {
        return false;
    }
    current->CallVoidMethod(host_, hosts->postToPage, text);
    current->DeleteLocalRef(text);
    android::clearException(current);
    return true;
}

AndroidView::AndroidView(std::shared_ptr<AndroidViewState> state)
    : state_(std::move(state))
{
}

AndroidView::~AndroidView()
{
    close();
}

InitializationState AndroidView::initializationState() const
{
    return state_->state()->initializationState();
}

void AndroidView::whenInitialized(InitializationCompletion completion)
{
    if (completion) {
        state_->state()->whenInitialized(std::move(completion));
    }
}

void AndroidView::attachNativeView()
{
    state_->attach();
}

void AndroidView::detachNativeView()
{
    state_->detach();
}

void AndroidView::open(WebApplicationPtr application, const std::string& route)
{
    state_->open(std::move(application), route);
}

void AndroidView::navigate(const Url& url)
{
    state_->navigate(url);
}

void AndroidView::loadDocument(const std::string& html, const Url& baseUrl)
{
    state_->loadDocument(html, baseUrl);
}

void AndroidView::stop()
{
    state_->stop();
}

void AndroidView::reload()
{
    state_->reload();
}

bool AndroidView::canGoBack() const
{
    return state_->canGoBack();
}

bool AndroidView::canGoForward() const
{
    return state_->canGoForward();
}

void AndroidView::goBack()
{
    state_->goBack();
}

void AndroidView::goForward()
{
    state_->goForward();
}

void AndroidView::close()
{
    state_->close();
}

bool AndroidView::isClosed() const
{
    return state_->isClosed();
}

WebViewBridge& AndroidView::bridge()
{
    return state_->bridge();
}

WebResourceManager& AndroidView::resources()
{
    return state_->resources();
}

void AndroidView::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    state_->setHostCallbacks(std::move(callbacks));
}
} // namespace webview

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_SystemWebViewJni_nativeOnPageStarted(JNIEnv* env, jclass, jlong handle, jstring url)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (view && url) {
        view->onPageStarted(webview::android::toUtf8(env, url));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_SystemWebViewJni_nativeOnPageCommitVisible(JNIEnv* env, jclass, jlong handle, jstring url)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (view && url) {
        view->onPageCommitVisible(webview::android::toUtf8(env, url));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_SystemWebViewJni_nativeOnPageFinished(JNIEnv* env, jclass, jlong handle, jstring url)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (view && url) {
        view->onPageFinished(webview::android::toUtf8(env, url));
    }
}

extern "C" JNIEXPORT void JNICALL Java_io_github_systemwebview_SystemWebViewJni_nativeOnReceivedError(
    JNIEnv* env,
    jclass,
    jlong handle,
    jstring url,
    jboolean mainFrame,
    jint code,
    jstring description)
{
    if (mainFrame != JNI_TRUE) {
        return;
    }
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (view) {
        view->onReceivedError(webview::android::toUtf8(env, url), static_cast<int>(code), webview::android::toUtf8(env, description));
    }
}

extern "C" JNIEXPORT jint JNICALL Java_io_github_systemwebview_SystemWebViewJni_nativeShouldOverrideUrlLoading(
    JNIEnv* env,
    jclass,
    jlong handle,
    jstring url,
    jboolean mainFrame,
    jboolean userInitiated,
    jboolean redirect)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (!view) {
        return 1;
    }
    return view->shouldOverrideUrlLoading(
        webview::android::toUtf8(env, url), mainFrame == JNI_TRUE, userInitiated == JNI_TRUE, redirect == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_systemwebview_SystemWebViewJni_nativeOnMessage(
    JNIEnv* env,
    jclass,
    jlong handle,
    jstring message,
    jstring sourceOrigin,
    jboolean mainFrame)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (view) {
        view->onMessage(
            webview::android::toUtf8(env, message), webview::android::toUtf8(env, sourceOrigin), mainFrame == JNI_TRUE);
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_io_github_systemwebview_SystemWebViewJni_nativeIntercept(
    JNIEnv* env,
    jclass,
    jlong handle,
    jstring url,
    jboolean mainFrame)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (!view) {
        return nullptr;
    }
    return view->intercept(env, webview::android::toUtf8(env, url), mainFrame == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_SystemWebViewJni_nativeOnRenderProcessGone(JNIEnv*, jclass, jlong handle, jint code)
{
    const auto view = webview::lookupView(static_cast<std::uint64_t>(handle));
    if (view) {
        view->onRenderProcessGone(static_cast<int>(code));
    }
}
