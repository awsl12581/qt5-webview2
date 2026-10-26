#pragma once

#include <system_webview/system_webview.h>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <objc/message.h>
#include <objc/runtime.h>
#endif
#include <GLFW/glfw3native.h>

namespace webview
{
#if defined(__APPLE__)
inline void* glfwCocoaContentView(GLFWwindow* window)
{
    void* cocoaWindow = glfwGetCocoaWindow(window);
    if (!cocoaWindow) {
        return nullptr;
    }
    using ContentViewFunction = void* (*)(void*, SEL);
    return reinterpret_cast<ContentViewFunction>(objc_msgSend)(cocoaWindow, sel_registerName("contentView"));
}
#endif

inline WebViewHost makeGlfwHost(GLFWwindow* window)
{
    WebViewHost host;
    if (!window) {
        return host;
    }
#if defined(_WIN32)
    host.nativeHandle = glfwGetWin32Window(window);
#elif defined(__APPLE__)
    host.nativeHandle = glfwCocoaContentView(window);
#endif
    glfwGetWindowSize(window, &host.width, &host.height);
    host.visible = glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
    host.refresh = [window](WebViewHost& current) {
#if defined(_WIN32)
        current.nativeHandle = glfwGetWin32Window(window);
#elif defined(__APPLE__)
        current.nativeHandle = glfwCocoaContentView(window);
#endif
        glfwGetWindowSize(window, &current.width, &current.height);
        current.visible = glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
    };
    host.postToUi = [](std::function<void()> action) {
        if (action) {
            action();
        }
    };
    return host;
}
} // namespace webview
