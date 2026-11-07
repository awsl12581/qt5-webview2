#pragma once

#include <jni.h>

#include <optional>
#include <string>
#include <string_view>

namespace webview::android
{
/// Java class that hosts the Android WebView. It is provided by the Android
/// host adapter and registered before any session is created.
constexpr auto kHostViewClassName = "io/github/systemwebview/SystemWebViewHostView";
constexpr auto kNativeClassName = "io/github/systemwebview/SystemWebViewJni";

JavaVM* javaVm();
void setJavaVm(JavaVM* vm);

/// Returns the JNIEnv for the calling thread. Detached native threads are
/// attached on demand; callers always run on the Java main thread.
JNIEnv* env();

void clearException(JNIEnv* env) noexcept;

/// Strict UTF-8 conversion across the UTF-16 JNI boundary.
std::string toUtf8(JNIEnv* env, jstring value);
jstring toJString(JNIEnv* env, std::string_view value);

struct HostMethods
{
    jclass clazz = nullptr;
    jmethodID attach = nullptr;
    jmethodID detach = nullptr;
    jmethodID loadUrl = nullptr;
    jmethodID loadDataWithBaseUrl = nullptr;
    jmethodID stopLoading = nullptr;
    jmethodID reloadPage = nullptr;
    jmethodID destroy = nullptr;
    jmethodID installDocumentScript = nullptr;
    jmethodID postToPage = nullptr;
    jmethodID canGoBack = nullptr;
    jmethodID goBack = nullptr;
    jmethodID canGoForward = nullptr;
    jmethodID goForward = nullptr;
    jmethodID assetOrigin = nullptr;
    jmethodID setProfileName = nullptr;
    jmethodID setNativeHandle = nullptr;
};

struct NativeMethods
{
    jclass clazz = nullptr;
    jmethodID isMainThread = nullptr;
    jmethodID supportsFeature = nullptr;
    jmethodID clearBrowsingData = nullptr;
};

/// Resolves and caches the Android host adapter bindings. Returns null when the
/// host classes are unavailable; the caller reports a typed initialization error.
const HostMethods* hostMethods(JNIEnv* env);
const NativeMethods* nativeMethods(JNIEnv* env);
} // namespace webview::android
