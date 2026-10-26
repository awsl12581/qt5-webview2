#include "internal/TextEncoding.h"
#include <system_webview/system_webview.h>

#include <windows.h>

#include <cstdint>
#include <format>
#include <gtest/gtest.h>
#include <mutex>
#include <string>

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

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM, LPARAM)
{
    return DefWindowProcW(window, message, 0, 0);
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
    return CreateWindowExW(0, className, className, WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}
} // namespace

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
