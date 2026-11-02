#include "internal/TextEncoding.h"
#include <system_webview/system_webview.h>

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

namespace
{
using HRESULT = long;
using PCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
extern "C" HRESULT __stdcall GetAvailableCoreWebView2BrowserVersionString(PCWSTR, LPWSTR*);
extern "C" void __stdcall CoTaskMemFree(void*);

std::string runtimeVersion()
{
    LPWSTR version = nullptr;
    const auto result = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    if (result < 0) {
        CoTaskMemFree(version);
        return std::format("unavailable:hresult=0x{:x}", static_cast<std::uint32_t>(result));
    }
    const auto value = webview::wideToUtf8(version ? version : L"");
    CoTaskMemFree(version);
    return value.value_or("unavailable:invalid_utf16");
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND makeHostWindow()
{
    static const wchar_t* className = L"SystemWebViewRuntimeProbe";
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW klass {};
        klass.lpfnWndProc = windowProc;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = className;
        RegisterClassW(&klass);
    });
    return CreateWindowExW(
        0,
        className,
        className,
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        640,
        480,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
}

HWND observedHost = nullptr;
int childWindowUpdates = 0;

LRESULT CALLBACK observeWindowUpdates(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && observedHost) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (IsChild(observedHost, message->hwnd) && (message->message == WM_WINDOWPOSCHANGING || message->message == WM_SHOWWINDOW)) {
            ++childWindowUpdates;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
} // namespace

TEST(WebView2Runtime, CrossOriginIframeLoadsExternalScriptAndStylesheet)
{
    const auto version = runtimeVersion();
    SCOPED_TRACE(version);
    ASSERT_FALSE(version.starts_with("unavailable:"));
    const auto window = makeHostWindow();
    ASSERT_NE(window, nullptr);
    const auto windowGuard = std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>(window, DestroyWindow);

    webview::WebViewPolicyConfig config;
    config.allowedAppHosts = { "iframe-main", "iframe-child" };
    config.pageToHostSchemas["iframe-result"] = { { { "script", webview::JsonType::String }, { "color", webview::JsonType::String } } };
    auto session = webview::createWebViewSession({}, webview::createDefaultWebViewPolicy(std::move(config)));
    ASSERT_TRUE(session);
    const auto assets = (std::filesystem::path(__FILE__).parent_path() / "assets" / "cross-origin-iframe").generic_string();
    webview::WebApplicationOptions mainOptions;
    mainOptions.id = "iframe-main";
    // The parent intentionally embeds a different local application origin.
    mainOptions.source = webview::LocalBundle { assets, "index.html", false, webview::ExternalNetworkAccess::Allowed };
    mainOptions.bridgeAccess = webview::BridgeAccess::Allowed;
    const auto mainApplication = session->createApplication(std::move(mainOptions));
    ASSERT_TRUE(mainApplication);
    webview::WebApplicationOptions childOptions;
    childOptions.id = "iframe-child";
    childOptions.source = webview::LocalBundle { assets, "child.html" };
    const auto childApplication = session->createApplication(std::move(childOptions));
    ASSERT_TRUE(childApplication);

    webview::WebViewHost host;
    host.nativeHandle = window;
    host.width = 640;
    host.height = 480;
    auto view = session->createWebView(std::move(host));
    ASSERT_TRUE(view);
    bool received = false;
    webview::Json result;
    view->bridge().setEventHandler("iframe-result", [&](const webview::Json& payload) {
        result = payload;
        received = true;
    });
    std::string initializationError;
    view->whenInitialized([&](const webview::InitializationResult& initialized) { initializationError = initialized.error; });
    view->open(mainApplication);
    view->attachNativeView();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!received && initializationError.empty() && std::chrono::steady_clock::now() < deadline) {
        MSG message {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    view->close();
    EXPECT_TRUE(initializationError.empty()) << initializationError;
    ASSERT_TRUE(received) << "No external iframe script result received";
    EXPECT_EQ(result.at("script"), "external-script-loaded");
    EXPECT_EQ(result.at("color"), "rgb(12, 34, 56)");
}

TEST(WebView2Runtime, VersionAndSessionLifecycle)
{
    ASSERT_FALSE(runtimeVersion().empty());
    HWND window = makeHostWindow();
    ASSERT_NE(window, nullptr);
    webview::WebViewHost host;
    host.nativeHandle = window;
    host.width = 640;
    host.height = 480;
    host.visible = false;
    auto session = webview::createWebViewSession({});
    ASSERT_TRUE(session);
    auto view = session->createWebView(host);
    ASSERT_TRUE(view);
    session.reset();
    ASSERT_TRUE(view->isClosed());
    bool completionCalled = false;
    view->bridge().call("closed-probe", webview::Json::object(), [&](const webview::Json&, const std::string& error) {
        completionCalled = !error.empty();
    });
    ASSERT_TRUE(completionCalled);
    view.reset();
    DestroyWindow(window);
}

TEST(WebView2Runtime, RepeatedAttachmentDoesNotUpdateChildWindows)
{
    const auto version = runtimeVersion();
    if (version.starts_with("unavailable:")) {
        GTEST_SKIP() << version;
    }
    HWND window = makeHostWindow();
    ASSERT_NE(window, nullptr);
    auto session = webview::createWebViewSession({});
    webview::WebViewHost host;
    host.nativeHandle = window;
    host.width = 640;
    host.height = 480;
    auto view = session->createWebView(std::move(host));
    view->attachNativeView();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (view->initializationState() == webview::InitializationState::Initializing && std::chrono::steady_clock::now() < deadline) {
        MSG message {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    ASSERT_EQ(view->initializationState(), webview::InitializationState::Ready);
    view->attachNativeView();
    observedHost = window;
    const auto hook = SetWindowsHookExW(WH_CALLWNDPROC, observeWindowUpdates, nullptr, GetCurrentThreadId());
    ASSERT_NE(hook, nullptr);
    childWindowUpdates = 0;
    for (int repeat = 0; repeat != 20; ++repeat) {
        view->attachNativeView();
    }
    const auto repeatedUpdates = childWindowUpdates;
    SetWindowPos(window, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    childWindowUpdates = 0;
    view->attachNativeView();
    const auto resizeUpdates = childWindowUpdates;
    childWindowUpdates = 0;
    view->attachNativeView();
    const auto unchangedUpdates = childWindowUpdates;
    UnhookWindowsHookEx(hook);
    observedHost = nullptr;
    view->close();
    view.reset();
    session.reset();
    DestroyWindow(window);
    EXPECT_EQ(repeatedUpdates, 0);
    EXPECT_GT(resizeUpdates, 0);
    EXPECT_EQ(unchangedUpdates, 0);
}
