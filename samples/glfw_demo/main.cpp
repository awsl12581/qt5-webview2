#include <system_webview/glfw_host.h>
#include <system_webview/system_webview.h>

#include <GLFW/glfw3.h>

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
            config.allowedAppHosts.insert("glfw3-demo");
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
    if (!glfwInit()) {
        return 1;
    }

    GLFWwindow* window = glfwCreateWindow(1000, 700, "System WebView GLFW3 Demo", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    auto session = webview::createWebViewSession({}, std::make_shared<DemoPolicy>());
    webview::WebApplicationOptions options;
    options.id = "glfw3-demo";
    options.source = webview::LocalBundle { SYSTEM_WEBVIEW_RESOURCE_DIR, "demo.html" };
    auto applicationSource = session->createApplication(std::move(options));
    auto view = session->createWebView(webview::makeGlfwHost(window));
    if (!applicationSource || !view) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    webview::WebViewPtr popup;
    webview::WebViewHostCallbacks callbacks;
    callbacks.onLoad = [](const webview::LoadEvent& event) {
        if (event.state == webview::LoadState::Finished) {
            std::clog << "GLFW3 demo page loaded: " << event.url.toString() << '\n';
        }
        else if (event.state == webview::LoadState::Failed) {
            std::cerr << "GLFW3 demo page failed: " << event.error << '\n';
        }
    };
    callbacks.createPopupHost = [window](const webview::NewWindowRequest&) { return webview::makeGlfwHost(window); };
    callbacks.onNewWindow = [&popup](const webview::NewWindowRequest&, webview::WebViewPtr child) {
        popup = std::move(child);
        popup->attachNativeView();
    };
    view->setHostCallbacks(std::move(callbacks));
    view->open(std::move(applicationSource));
    view->attachNativeView();

    while (!glfwWindowShouldClose(window) && !view->isClosed()) {
        glfwPollEvents();
        // GLFW does not expose a host-change callback. Refresh after polling
        // so the first Cocoa layout and subsequent resizes can attach the view.
        if (!popup) {
            view->attachNativeView();
        }
    }
    view->detachNativeView();
    view->close();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
