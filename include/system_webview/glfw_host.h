#pragma once

#include <system_webview/system_webview.h>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

namespace webview
{
inline WebViewHost makeGlfwHost(GLFWwindow* window)
{
    WebViewHost host;
    if (!window) {
        return host;
    }
#if defined(_WIN32)
    host.nativeHandle = glfwGetWin32Window(window);
#elif defined(__APPLE__)
    host.nativeHandle = glfwGetCocoaWindow(window);
#endif
    glfwGetWindowSize(window, &host.width, &host.height);
    host.visible = glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
    host.postToUi = [](std::function<void()> action) {
        if (action) {
            action();
        }
    };
    return host;
}
} // namespace webview
