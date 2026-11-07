#include "platform/android/AndroidSession.h"

#include "platform/android/AndroidJni.h"
#include "platform/android/AndroidView.h"

#include "internal/Application.h"
#include "internal/ResourceMapping.h"
#include "webview/WebViewState.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace webview
{
std::mutex pendingMutex;
std::unordered_map<std::int64_t, IWebViewSession::ClearCompletion> pendingClears;
std::int64_t nextClearId = 1;

namespace
{
std::string featureCheckError(const char* feature)
{
    return std::string("Android System WebView does not support ") + feature + ". Update Android System WebView.";
}

bool androidSupports(JNIEnv* env, const android::NativeMethods& natives, const char* feature)
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
} // namespace

AndroidSessionState::AndroidSessionState(WebViewSessionOptions options, WebViewPolicyPtr policy)
    : options(std::move(options))
    , policy(policy ? std::move(policy) : createDefaultWebViewPolicy())
    , scheduler(std::make_shared<WebViewState>(DiagnosticScope::Session))
{
}

AndroidSessionState::~AndroidSessionState() = default;

AndroidSession::AndroidSession(WebViewSessionOptions options, WebViewPolicyPtr policy)
    : state_(std::make_shared<AndroidSessionState>(std::move(options), std::move(policy)))
{
    JNIEnv* env = android::env();
    if (!env) {
        state_->scheduler->failInitialization(
            "The Android JNI environment is unavailable. Load libsystem_webview from the Java host first.");
        return;
    }
    const auto* natives = android::nativeMethods(env);
    if (!natives) {
        state_->scheduler->failInitialization(
            "The Android host adapter (io.github.systemwebview.SystemWebViewJni) is unavailable.");
        return;
    }
    if (!env->CallStaticBooleanMethod(natives->clazz, natives->isMainThread)) {
        android::clearException(env);
        state_->scheduler->failInitialization("The Android backend requires the Java main thread.");
        return;
    }
    if (state_->options.mode == SessionMode::Ephemeral && !androidSupports(env, *natives, "MULTI_PROFILE")) {
        state_->scheduler->failInitialization(featureCheckError("MULTI_PROFILE (required for ephemeral sessions)"));
        return;
    }
    state_->scheduler->markReady();
}

AndroidSession::~AndroidSession()
{
    if (!state_) {
        return;
    }
    state_->closed = true;
    state_->callbacks = {};
    auto closers = std::move(state_->viewClosers);
    state_->viewClosers.clear();
    for (auto& closer : closers) {
        if (closer) {
            closer();
        }
    }
    state_->scheduler->close();
}

InitializationState AndroidSession::initializationState() const
{
    return state_->scheduler->initializationState();
}

void AndroidSession::whenInitialized(InitializationCompletion completion)
{
    state_->scheduler->whenInitialized(std::move(completion));
}

WebApplicationPtr AndroidSession::createApplication(WebApplicationOptions options)
{
    std::string error;
    const auto application = webview::createApplication(std::move(options), &error);
    if (!application || state_->applicationIds.contains(application->id())) {
        return {};
    }
    if (const auto* bundle = std::get_if<LocalBundle>(&application->source())) {
        std::vector<ResourceMapping> candidate = *state_->resourceMappings;
        candidate.push_back(
            { application->origin(), bundle->directory, bundle->entryDocument, bundle->spaFallback, bundle->externalNetworkAccess });
        if (!validateResourceMappings(&candidate, &error)
            || resolveMappedResource(candidate.back(), application->urlForRoute({}), &error).empty()) {
            return {};
        }
        *state_->resourceMappings = std::move(candidate);
    }
    state_->applicationIds.insert(application->id());
    state_->applications.push_back(application);
    return application;
}

WebViewPtr AndroidSession::createWebView(WebViewHost host)
{
    JNIEnv* env = android::env();
    auto viewState = std::make_shared<AndroidViewState>(
        env, static_cast<jobject>(host.nativeHandle), state_, state_->options.mode);
    auto view = std::make_unique<AndroidView>(viewState);
    viewState->start();
    return view;
}

void AndroidSession::clearCache(ClearCompletion completion)
{
    requestClear(0, std::move(completion));
}

void AndroidSession::clearCookies(ClearCompletion completion)
{
    requestClear(1, std::move(completion));
}

void AndroidSession::clearWebsiteData(ClearCompletion completion)
{
    requestClear(2, std::move(completion));
}

void AndroidSession::requestClear(int kind, ClearCompletion completion)
{
    if (!completion) {
        return;
    }
    if (state_->scheduler->initializationState() != InitializationState::Ready) {
        completion({ false, "The Android session is not ready." });
        return;
    }
    JNIEnv* env = android::env();
    const auto* natives = env ? android::nativeMethods(env) : nullptr;
    if (!natives) {
        completion({ false, "The Android host adapter is unavailable." });
        return;
    }
    std::int64_t requestId = 0;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        requestId = nextClearId++;
        pendingClears.insert_or_assign(requestId, std::move(completion));
    }
    env->CallStaticVoidMethod(natives->clazz, natives->clearBrowsingData, static_cast<jlong>(requestId), static_cast<jint>(kind));
    if (env->ExceptionCheck()) {
        android::clearException(env);
        ClearCompletion failed;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            const auto found = pendingClears.find(requestId);
            if (found != pendingClears.end()) {
                failed = std::move(found->second);
                pendingClears.erase(found);
            }
        }
        if (failed) {
            failed({ false, "Android data clearing failed." });
        }
    }
}

void AndroidSession::setHostCallbacks(WebViewSessionHostCallbacks callbacks)
{
    if (!state_->closed) {
        state_->callbacks = std::move(callbacks);
    }
}

bool AndroidSession::supports(WebViewCapability capability) const
{
    switch (capability) {
    case WebViewCapability::PersistentProfile:
    case WebViewCapability::NavigationHistory:
        return true;
    case WebViewCapability::PrivateProfile: {
        JNIEnv* env = android::env();
        const auto* natives = env ? android::nativeMethods(env) : nullptr;
        return natives && androidSupports(env, *natives, "MULTI_PROFILE");
    }
    case WebViewCapability::FileSelection:
    case WebViewCapability::DownloadDefault:
    case WebViewCapability::DownloadTarget:
    case WebViewCapability::Camera:
    case WebViewCapability::Microphone:
    case WebViewCapability::Location:
    case WebViewCapability::Notifications:
    case WebViewCapability::Clipboard:
        return false;
    }
    return false;
}
} // namespace webview

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_SystemWebViewJni_nativeOnClearComplete(JNIEnv*, jclass, jlong requestId, jboolean success)
{
    webview::IWebViewSession::ClearCompletion completion;
    {
        std::lock_guard<std::mutex> lock(webview::pendingMutex);
        const auto found = webview::pendingClears.find(static_cast<std::int64_t>(requestId));
        if (found == webview::pendingClears.end()) {
            return;
        }
        completion = std::move(found->second);
        webview::pendingClears.erase(found);
    }
    if (completion) {
        completion({ success == JNI_TRUE, success == JNI_TRUE ? std::string() : std::string("Android data clearing failed.") });
    }
}
