#include <system_webview/system_webview.h>

#include <android/log.h>
#include <jni.h>

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto kTag = "SystemWebViewDemo";

JavaVM* globalVm = nullptr;

JNIEnv* currentEnv()
{
    if (!globalVm) {
        return nullptr;
    }
    JNIEnv* env = nullptr;
    if (globalVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return nullptr;
    }
    return env;
}

char32_t decodeUtf8(std::string_view text, std::size_t* offset)
{
    const auto first = static_cast<unsigned char>(text[*offset]);
    std::size_t length = 1;
    char32_t code = first;
    if ((first & 0xE0) == 0xC0) {
        length = 2;
        code = first & 0x1F;
    }
    else if ((first & 0xF0) == 0xE0) {
        length = 3;
        code = first & 0x0F;
    }
    else if ((first & 0xF8) == 0xF0) {
        length = 4;
        code = first & 0x07;
    }
    else if (first >= 0x80) {
        ++*offset;
        return U'\uFFFD';
    }
    if (*offset + length > text.size()) {
        ++*offset;
        return U'\uFFFD';
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(text[*offset + index]);
        if ((next & 0xC0) != 0x80) {
            ++*offset;
            return U'\uFFFD';
        }
        code = (code << 6) | (next & 0x3F);
    }
    *offset += length;
    return code;
}

jstring createString(JNIEnv* env, std::string_view text)
{
    if (!env) {
        return nullptr;
    }
    std::u16string converted;
    converted.reserve(text.size());
    for (std::size_t offset = 0; offset < text.size();) {
        const char32_t code = decodeUtf8(text, &offset);
        if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            converted.push_back(static_cast<char16_t>(0xFFFD));
        }
        else if (code > 0xFFFF) {
            const char32_t value = code - 0x10000;
            converted.push_back(static_cast<char16_t>(0xD800 + (value >> 10)));
            converted.push_back(static_cast<char16_t>(0xDC00 + (value & 0x3FF)));
        }
        else {
            converted.push_back(static_cast<char16_t>(code));
        }
    }
    if (converted.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) {
        return nullptr;
    }
    return env->NewString(
        reinterpret_cast<const jchar*>(converted.data()), static_cast<jsize>(converted.size()));
}

std::string fromJString(JNIEnv* env, jstring value)
{
    if (!env || !value) {
        return {};
    }
    const jsize length = env->GetStringLength(value);
    if (length <= 0) {
        return {};
    }
    std::u16string text(static_cast<std::size_t>(length), u'\0');
    env->GetStringRegion(value, 0, length, reinterpret_cast<jchar*>(text.data()));
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        char32_t code = text[index];
        if (code >= 0xD800 && code <= 0xDBFF && index + 1 < text.size()) {
            const char32_t low = text[index + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                ++index;
            }
        }
        if (code < 0x80) {
            result.push_back(static_cast<char>(code));
        }
        else if (code < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (code >> 6)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else if (code < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (code >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        else {
            result.push_back(static_cast<char>(0xF0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return result;
}

void logLine(const std::string& message)
{
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message.c_str());
}

struct DemoHost
{
    webview::WebViewSessionPtr session;
    webview::WebViewPtr view;
    jobject hostObject = nullptr;
    jclass hostClass = nullptr;
    jmethodID dispatchLoad = nullptr;
    jmethodID dispatchBridgeEvent = nullptr;
    bool maximized = false;
};

DemoHost& demo()
{
    static DemoHost instance;
    return instance;
}

void dispatchLoad(webview::LoadState state, const std::string& url, const std::string& error)
{
    DemoHost& host = demo();
    JNIEnv* env = currentEnv();
    if (!env || !host.hostObject || !host.dispatchLoad) {
        return;
    }
    jstring target = createString(env, url);
    jstring message = createString(env, error);
    env->CallVoidMethod(host.hostObject, host.dispatchLoad, static_cast<jint>(state), target, message);
    if (target) {
        env->DeleteLocalRef(target);
    }
    if (message) {
        env->DeleteLocalRef(message);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

void dispatchBridgeEvent(const std::string& type, const std::string& payload)
{
    DemoHost& host = demo();
    JNIEnv* env = currentEnv();
    if (!env || !host.hostObject || !host.dispatchBridgeEvent) {
        return;
    }
    jstring eventType = createString(env, type);
    jstring eventPayload = createString(env, payload);
    env->CallVoidMethod(host.hostObject, host.dispatchBridgeEvent, eventType, eventPayload);
    if (eventType) {
        env->DeleteLocalRef(eventType);
    }
    if (eventPayload) {
        env->DeleteLocalRef(eventPayload);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

class DemoPolicy final : public webview::WebViewPolicy
{
public:
    DemoPolicy()
        : webview::WebViewPolicy(config())
    {
    }

    webview::PermissionDecision decidePermission(const webview::PermissionRequest&) const override
    {
        return webview::PermissionDecision::Deny;
    }

private:
    static webview::WebViewPolicyConfig config()
    {
        webview::WebViewPolicyConfig value;
        value.allowedAppHosts.insert("demo");
        value.pageToHostSchemas["hello"] = { { { "message", webview::JsonType::String } } };
        value.pageToHostSchemas["window"] = { { { "action", webview::JsonType::String } } };
        value.hostToPageSchemas["ack"] = { { { "message", webview::JsonType::String } } };
        value.hostToPageSchemas["window-state"] = { { { "maximized", webview::JsonType::Boolean } } };
        return value;
    }
};

void stopSession(JNIEnv* env)
{
    DemoHost& host = demo();
    if (host.view) {
        host.view->detachNativeView();
        host.view->close();
        host.view.reset();
    }
    host.session.reset();
    if (env) {
        if (host.hostObject) {
            env->DeleteGlobalRef(host.hostObject);
        }
        if (host.hostClass) {
            env->DeleteGlobalRef(host.hostClass);
        }
    }
    host.hostObject = nullptr;
    host.hostClass = nullptr;
    host.dispatchLoad = nullptr;
    host.dispatchBridgeEvent = nullptr;
    host.maximized = false;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*)
{
    globalVm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_demo_MainActivity_nativeStart(JNIEnv* env, jobject, jobject hostView, jstring directory)
{
    if (!env || !hostView || !directory) {
        return;
    }
    stopSession(env);
    DemoHost& host = demo();
    host.hostObject = env->NewGlobalRef(hostView);
    jclass localClass = env->GetObjectClass(hostView);
    host.hostClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    env->DeleteLocalRef(localClass);
    host.dispatchLoad = env->GetMethodID(host.hostClass, "dispatchLoad", "(ILjava/lang/String;Ljava/lang/String;)V");
    host.dispatchBridgeEvent
        = env->GetMethodID(host.hostClass, "dispatchBridgeEvent", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        logLine("Android host adapter methods are unavailable.");
        stopSession(env);
        return;
    }
    const std::string bundleDirectory = fromJString(env, directory);
    if (bundleDirectory.empty()) {
        logLine("The Android bundle directory is empty.");
        stopSession(env);
        return;
    }

    webview::WebViewSessionOptions sessionOptions;
    sessionOptions.mode = webview::SessionMode::Persistent;
    sessionOptions.diagnosticHandler = [](const webview::DiagnosticEvent& event) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "[%s] %s", event.category.c_str(), event.message.c_str());
    };
    host.session = webview::createWebViewSession(std::move(sessionOptions), std::make_shared<DemoPolicy>());
    if (!host.session) {
        logLine("Session creation failed.");
        stopSession(env);
        return;
    }
    if (host.session->initializationState() != webview::InitializationState::Ready) {
        dispatchLoad(webview::LoadState::Failed, {}, "会话初始化失败。");
        stopSession(env);
        return;
    }

    webview::WebApplicationOptions applicationOptions;
    applicationOptions.id = "demo";
    applicationOptions.source = webview::LocalBundle { bundleDirectory, "demo.html" };
    applicationOptions.bridgeAccess = webview::BridgeAccess::Allowed;
    auto application = host.session->createApplication(std::move(applicationOptions));
    if (!application) {
        dispatchLoad(webview::LoadState::Failed, {}, "无法注册本地页面资源目录。");
        stopSession(env);
        return;
    }

    webview::WebViewHost webViewHost;
    webViewHost.nativeHandle = hostView;
    webViewHost.visible = true;
    host.view = host.session->createWebView(webViewHost);
    if (!host.view) {
        dispatchLoad(webview::LoadState::Failed, {}, "无法创建 Android WebView。");
        stopSession(env);
        return;
    }

    webview::WebViewHostCallbacks callbacks;
    callbacks.onLoad = [](const webview::LoadEvent& event) {
        dispatchLoad(event.state, event.url.toString(), event.error);
    };
    callbacks.onOpenExternal = [](const webview::Url& url) {
        DemoHost& state = demo();
        JNIEnv* current = currentEnv();
        if (!current || !state.hostObject) {
            return;
        }
        jclass target = current->GetObjectClass(state.hostObject);
        const jmethodID openExternal = current->GetMethodID(target, "openExternal", "(Ljava/lang/String;)V");
        if (openExternal) {
            jstring text = createString(current, url.toString());
            current->CallVoidMethod(state.hostObject, openExternal, text);
            if (text) {
                current->DeleteLocalRef(text);
            }
        }
        current->DeleteLocalRef(target);
        if (current->ExceptionCheck()) {
            current->ExceptionClear();
        }
    };
    callbacks.onRuntimeFailure = [](const webview::RuntimeFailureEvent& event) {
        dispatchLoad(webview::LoadState::Failed, {}, event.reason);
    };
    host.view->setHostCallbacks(std::move(callbacks));

    host.view->bridge().setEventHandler("hello", [](const webview::Json& payload) {
        const std::string message = payload.value("message", std::string());
        logLine("Page message: " + message);
        webview::Json ack;
        ack["message"] = "Native received your message.";
        if (demo().view) {
            demo().view->bridge().emitEvent("ack", ack);
        }
    });
    host.view->bridge().setEventHandler("window", [](const webview::Json& payload) {
        const std::string action = payload.value("action", std::string());
        if (action == "maximize") {
            demo().maximized = !demo().maximized;
            webview::Json state;
            state["maximized"] = demo().maximized;
            if (demo().view) {
                demo().view->bridge().emitEvent("window-state", state);
            }
        }
        webview::Json forwarded;
        forwarded["action"] = action;
        dispatchBridgeEvent("window", forwarded.dump());
    });

    host.view->open(application);
    host.view->attachNativeView();
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_demo_MainActivity_nativeStop(JNIEnv* env, jobject)
{
    stopSession(env);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_systemwebview_demo_MainActivity_nativeCanGoBack(JNIEnv*, jobject)
{
    return demo().view && demo().view->canGoBack() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_demo_MainActivity_nativeGoBack(JNIEnv*, jobject)
{
    if (demo().view) {
        demo().view->goBack();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_systemwebview_demo_MainActivity_nativeReload(JNIEnv*, jobject)
{
    if (demo().view) {
        demo().view->reload();
    }
}
