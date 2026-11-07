#include "platform/android/AndroidJni.h"

#include "internal/TextEncoding.h"

#include <limits>
#include <mutex>
#include <utility>

namespace webview::android
{
namespace
{
std::mutex vmMutex;
JavaVM* globalVm = nullptr;

std::mutex methodsMutex;
std::optional<HostMethods> hostBindings;
std::optional<NativeMethods> nativeBindings;
bool hostResolved = false;
bool nativeResolved = false;

jmethodID method(JNIEnv* env, jclass clazz, const char* name, const char* signature)
{
    return env->GetMethodID(clazz, name, signature);
}

jmethodID staticMethod(JNIEnv* env, jclass clazz, const char* name, const char* signature)
{
    return env->GetStaticMethodID(clazz, name, signature);
}
}

JavaVM* javaVm()
{
    std::lock_guard<std::mutex> lock(vmMutex);
    return globalVm;
}

void setJavaVm(JavaVM* vm)
{
    std::lock_guard<std::mutex> lock(vmMutex);
    globalVm = vm;
}

JNIEnv* env()
{
    JavaVM* vm = javaVm();
    if (!vm) {
        return nullptr;
    }
    JNIEnv* current = nullptr;
    const jint result = vm->GetEnv(reinterpret_cast<void**>(&current), JNI_VERSION_1_6);
    if (result == JNI_OK) {
        return current;
    }
    if (result == JNI_EDETACHED && vm->AttachCurrentThread(&current, nullptr) == JNI_OK) {
        return current;
    }
    return nullptr;
}

void clearException(JNIEnv* current) noexcept
{
    if (current && current->ExceptionCheck()) {
        current->ExceptionClear();
    }
}

std::string toUtf8(JNIEnv* current, jstring value)
{
    if (!current || !value) {
        return {};
    }
    const jsize length = current->GetStringLength(value);
    if (length <= 0) {
        return {};
    }
    std::u16string text(static_cast<std::size_t>(length), u'\0');
    current->GetStringRegion(value, 0, length, reinterpret_cast<jchar*>(text.data()));
    clearException(current);
    const auto converted = utf16ToUtf8(text);
    return converted ? *converted : std::string();
}

jstring toJString(JNIEnv* current, std::string_view value)
{
    if (!current) {
        return nullptr;
    }
    const auto converted = utf8ToUtf16(value);
    if (!converted) {
        return nullptr;
    }
    const std::size_t length = converted->size();
    if (length > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) {
        return nullptr;
    }
    return current->NewString(reinterpret_cast<const jchar*>(converted->data()), static_cast<jsize>(length));
}

const HostMethods* hostMethods(JNIEnv* current)
{
    if (!current) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(methodsMutex);
    if (hostResolved) {
        return hostBindings ? &*hostBindings : nullptr;
    }
    hostResolved = true;
    jclass local = current->FindClass(kHostViewClassName);
    if (!local) {
        clearException(current);
        return nullptr;
    }
    HostMethods methods;
    methods.clazz = static_cast<jclass>(current->NewGlobalRef(local));
    current->DeleteLocalRef(local);
    if (!methods.clazz) {
        return nullptr;
    }
    methods.attach = method(current, methods.clazz, "attachWebView", "()V");
    methods.detach = method(current, methods.clazz, "detachWebView", "()V");
    methods.loadUrl = method(current, methods.clazz, "loadUrl", "(Ljava/lang/String;)V");
    methods.loadDataWithBaseUrl = method(current, methods.clazz, "loadDataWithBaseUrl", "(Ljava/lang/String;Ljava/lang/String;)V");
    methods.stopLoading = method(current, methods.clazz, "stopLoading", "()V");
    methods.reloadPage = method(current, methods.clazz, "reloadPage", "()V");
    methods.destroy = method(current, methods.clazz, "destroyWebView", "()V");
    methods.installDocumentScript
        = method(current, methods.clazz, "installDocumentScript", "(Ljava/lang/String;)V");
    methods.postToPage = method(current, methods.clazz, "postToPage", "(Ljava/lang/String;)V");
    methods.canGoBack = method(current, methods.clazz, "canGoBack", "()Z");
    methods.goBack = method(current, methods.clazz, "goBack", "()V");
    methods.canGoForward = method(current, methods.clazz, "canGoForward", "()Z");
    methods.goForward = method(current, methods.clazz, "goForward", "()V");
    methods.assetOrigin = method(current, methods.clazz, "assetOrigin", "()Ljava/lang/String;");
    methods.setProfileName = method(current, methods.clazz, "setProfileName", "(Ljava/lang/String;)V");
    methods.setNativeHandle = method(current, methods.clazz, "setNativeHandle", "(J)V");
    clearException(current);
    if (!methods.attach || !methods.detach || !methods.loadUrl || !methods.loadDataWithBaseUrl || !methods.stopLoading
        || !methods.reloadPage || !methods.destroy || !methods.installDocumentScript || !methods.postToPage
        || !methods.canGoBack || !methods.goBack || !methods.canGoForward || !methods.goForward || !methods.assetOrigin
        || !methods.setProfileName || !methods.setNativeHandle) {
        current->DeleteGlobalRef(methods.clazz);
        return nullptr;
    }
    hostBindings = methods;
    return &*hostBindings;
}

const NativeMethods* nativeMethods(JNIEnv* current)
{
    if (!current) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(methodsMutex);
    if (nativeResolved) {
        return nativeBindings ? &*nativeBindings : nullptr;
    }
    nativeResolved = true;
    jclass local = current->FindClass(kNativeClassName);
    if (!local) {
        clearException(current);
        return nullptr;
    }
    NativeMethods methods;
    methods.clazz = static_cast<jclass>(current->NewGlobalRef(local));
    current->DeleteLocalRef(local);
    if (!methods.clazz) {
        return nullptr;
    }
    methods.isMainThread = staticMethod(current, methods.clazz, "isMainThread", "()Z");
    methods.supportsFeature = staticMethod(current, methods.clazz, "supportsFeature", "(Ljava/lang/String;)Z");
    methods.clearBrowsingData = staticMethod(current, methods.clazz, "clearBrowsingData", "(JI)V");
    clearException(current);
    if (!methods.isMainThread || !methods.supportsFeature || !methods.clearBrowsingData) {
        current->DeleteGlobalRef(methods.clazz);
        return nullptr;
    }
    nativeBindings = methods;
    return &*nativeBindings;
}
} // namespace webview::android

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*)
{
    webview::android::setJavaVm(vm);
    return JNI_VERSION_1_6;
}
