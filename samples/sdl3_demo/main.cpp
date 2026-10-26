#include <system_webview/sdl3_host.h>
#include <system_webview/system_webview.h>

#include <SDL3/SDL.h>

#include <iostream>
#include <utility>

namespace
{
class DemoPolicy final : public webview::WebViewPolicy
{
public:
    DemoPolicy()
        : WebViewPolicy([] {
            webview::WebViewPolicyConfig config;
            config.allowedAppHosts.insert("sdl3-demo");
            return config;
        }())
    {
    }

    webview::NewWindowDecision decideNewWindow(const webview::NewWindowRequest& request) const override
    {
        return request.url == webview::Url("https://example.com/") ? webview::NewWindowDecision::Allow
                                                                     : webview::NewWindowDecision::Cancel;
    }
};
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("System WebView SDL3 Demo", 1000, 700, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Quit();
        return 1;
    }

    auto session = webview::createWebViewSession({}, std::make_shared<DemoPolicy>());
    webview::WebApplicationOptions options;
    options.id = "sdl3-demo";
    options.source = webview::LocalBundle { SYSTEM_WEBVIEW_RESOURCE_DIR, "demo.html" };
    auto applicationSource = session->createApplication(std::move(options));
    auto view = session->createWebView(webview::makeSdl3Host(window));
    if (!applicationSource || !view) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    webview::WebViewPtr popup;
    webview::WebViewHostCallbacks callbacks;
    callbacks.onLoad = [](const webview::LoadEvent& event) {
        if (event.state == webview::LoadState::Finished) {
            std::clog << "SDL3 demo page loaded: " << event.url.toString() << '\n';
        }
        else if (event.state == webview::LoadState::Failed) {
            std::cerr << "SDL3 demo page failed: " << event.error << '\n';
        }
    };
    callbacks.createPopupHost = [window](const webview::NewWindowRequest&) { return webview::makeSdl3Host(window); };
    callbacks.onNewWindow = [&popup](const webview::NewWindowRequest&, webview::WebViewPtr child) {
        popup = std::move(child);
        popup->attachNativeView();
    };
    view->setHostCallbacks(std::move(callbacks));
    view->open(std::move(applicationSource));
    view->attachNativeView();

    bool running = true;
    SDL_Event event;
    while (running && !view->isClosed()) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        // SDL does not expose a host-change callback. Refresh after polling
        // so the first Cocoa layout and subsequent resizes can attach the view.
        if (!popup) {
            view->attachNativeView();
        }
    }
    view->detachNativeView();
    view->close();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
