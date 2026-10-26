#pragma once

#include <system_webview/system_webview.h>

#include <SDL3/SDL.h>

namespace webview
{
inline WebViewHost makeSdl3Host(SDL_Window* window)
{
    WebViewHost host;
    if (!window) {
        return host;
    }
    SDL_GetWindowSize(window, &host.width, &host.height);
    host.visible = (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0;
#if defined(_WIN32)
    host.nativeHandle = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    host.nativeHandle = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif
    host.postToUi = [](std::function<void()> action) {
        if (action) {
            action();
        }
    };
    return host;
}
} // namespace webview
