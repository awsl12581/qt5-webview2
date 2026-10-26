#pragma once

#include <system_webview/system_webview.h>

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace webview
{
#if defined(__APPLE__)
inline void* sdl3CocoaContentView(SDL_Window* window)
{
    void* cocoaWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!cocoaWindow) {
        return nullptr;
    }
    using ContentViewFunction = void* (*)(void*, SEL);
    return reinterpret_cast<ContentViewFunction>(objc_msgSend)(cocoaWindow, sel_registerName("contentView"));
}
#endif

inline WebViewHost makeSdl3Host(SDL_Window* window)
{
    WebViewHost host;
    if (!window) {
        return host;
    }
    SDL_GetWindowSize(window, &host.width, &host.height);
    host.visible = (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0;
    host.refresh = [window](WebViewHost& current) {
        SDL_GetWindowSize(window, &current.width, &current.height);
        current.visible = (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0;
#if defined(_WIN32)
        current.nativeHandle = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
        current.nativeHandle = sdl3CocoaContentView(window);
#endif
    };
#if defined(_WIN32)
    host.nativeHandle = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    host.nativeHandle = sdl3CocoaContentView(window);
#endif
    host.postToUi = [](std::function<void()> action) {
        if (action) {
            action();
        }
    };
    return host;
}
} // namespace webview
