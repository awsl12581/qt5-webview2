#include <system_webview/system_webview.h>

#include "NativeTestSupport.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace
{
class TestPolicy final : public webview::WebViewPolicy
{
public:
    explicit TestPolicy(webview::WebViewPolicyConfig config)
        : WebViewPolicy(std::move(config))
    {
    }

    webview::NavigationDecision decideNavigation(const webview::NavigationRequest& request) const override
    {
        navigationRequests.push_back(request);
        if (request.url.scheme() == "http" && request.url.host() == "127.0.0.1") {
            return webview::NavigationDecision::Allow;
        }
        return WebViewPolicy::decideNavigation(request);
    }

    webview::NewWindowDecision decideNewWindow(const webview::NewWindowRequest&) const override
    {
        return allowPopups ? webview::NewWindowDecision::Allow : webview::NewWindowDecision::Cancel;
    }

    webview::PermissionDecision decidePermission(const webview::PermissionRequest& request) const override
    {
        permissionRequests.push_back(request);
        return webview::PermissionDecision::Deny;
    }

    webview::DownloadDecision decideDownload(const webview::DownloadRequest& request) const override
    {
        downloadRequests.push_back(request);
        return webview::DownloadDecision::Cancel;
    }

    mutable std::vector<webview::NavigationRequest> navigationRequests;
    mutable std::vector<webview::PermissionRequest> permissionRequests;
    mutable std::vector<webview::DownloadRequest> downloadRequests;
    bool allowPopups = false;
};

struct ServerStats
{
    int reloadRequests = 0;
    int slowRequests = 0;
};

std::string serveConnection(std::string_view request, std::uint16_t port, ServerStats* stats)
{
        std::string response;
        if (request.starts_with("GET /redirect ")) {
            response = "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(port)
                + "/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        }
        else if (request.starts_with("GET /download ")) {
            const std::string body = "download-data";
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                       "Content-Disposition: attachment; filename=test.bin\r\nContent-Length: "
                + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        }
        else if (request.starts_with("GET /slow ")) {
            ++stats->slowRequests;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const std::string body = "<!doctype html><title>slow</title>done";
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(body.size())
                + "\r\nConnection: close\r\n\r\n" + body;
        }
        else {
            if (request.starts_with("GET /reload ")) {
                ++stats->reloadRequests;
            }
            const std::string body = "<!doctype html><title>final</title>done";
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(body.size())
                + "\r\nConnection: close\r\n\r\n" + body;
        }
        return response;
}
}

TEST(MacOSView, NavigationBridgeAndLifecycle)
{
    webview::WebViewPolicyConfig config;
    config.allowedAppHosts.insert("bridge-test");
    config.trustedHttpsOrigins.insert("https://trusted.example");
    config.pageToHostSchemas["hello"] = { { { "message", webview::JsonType::String } }, true };
    config.hostToPageSchemas["hello"] = { { { "message", webview::JsonType::String } } };
    auto policy = std::make_shared<TestPolicy>(std::move(config));
    auto session = webview::createWebViewSession({}, policy);
    auto view = session->createWebView({});

    std::vector<webview::LoadEvent> events;
    int messageCount = 0;
    int popupCount = 0;
    bool hostileReturned = false;
    bool hostileExecuted = false;
    std::string hostileReturnedPayload;
    native_test::RunLoop loop;

    webview::WebViewHostCallbacks callbacks;
    callbacks.onLoad = [&](const webview::LoadEvent& event) {
        events.push_back(event);
        if (event.state == webview::LoadState::Finished || event.state == webview::LoadState::Failed) {
            loop.quit();
        }
    };
    callbacks.onNewWindow = [&](const webview::NewWindowRequest&, webview::WebViewPtr) { ++popupCount; };
    view->setHostCallbacks(std::move(callbacks));
    view->bridge().setEventHandler("hello", [&](const webview::Json& payload) {
        ++messageCount;
        if (payload.contains("executed")) {
            hostileReturned = true;
            hostileExecuted = payload.at("executed").get<bool>();
            hostileReturnedPayload = payload.at("message").get<std::string>();
            loop.quit();
        }
        else {
            ASSERT_EQ(payload.at("message").get<std::string>(), "main");
            ASSERT_TRUE(payload.at("resourceVerified").get<bool>());
        }
    });
    const std::string html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"></head><body>
<script src="脚本.js"></script>
<iframe srcdoc="<script>window.webkit.messageHandlers.systemWebView.postMessage({version:1,type:'hello',payload:{message:'frame'}})</script>"></iframe>
<script>
window.addEventListener('DOMContentLoaded', () => {
  window.addEventListener('system-webview-message', event => {
    document.body.dataset.nativeMessage = event.detail.payload.message;
    window.systemWebView.postMessage({
      version: 1,
      type: 'hello',
      payload: {
        message: event.detail.payload.message,
        executed: window.__payloadExecuted === true
      }
    });
  });
  window.systemWebView.postMessage({version:1,type:'hello',payload:{
    message:'main',
    resourceVerified: window.resourceText.length === 300 * 1024 + 2 && window.resourceText.endsWith('结束')
  }});
  window.open('https://trusted.example/popup');
});
</script>
</body></html>)HTML";
    native_test::TempDir bridgeRoot;
    ASSERT_TRUE(bridgeRoot.isValid());
    const auto scriptPath = bridgeRoot.filePath("脚本.js");
    const auto scriptBytes = "window.resourceText='" + std::string(300 * 1024, 'x') + "结束';";
    ASSERT_TRUE(native_test::writeFile(scriptPath, scriptBytes));
    const auto bridgeEntry = bridgeRoot.filePath("index.html");
    ASSERT_TRUE(native_test::writeFile(bridgeEntry, html));
    webview::WebApplicationOptions bridgeApplicationOptions;
    bridgeApplicationOptions.id = "bridge-test";
    bridgeApplicationOptions.source = webview::LocalBundle { bridgeRoot.path() };
    bridgeApplicationOptions.bridgeAccess = webview::BridgeAccess::Allowed;
    auto bridgeApplication = session->createApplication(std::move(bridgeApplicationOptions));
    ASSERT_TRUE(bridgeApplication);
    view->open(bridgeApplication);
    loop.exec(std::chrono::seconds(10));

    ASSERT_TRUE(!events.empty());
    ASSERT_EQ(events.front().state, webview::LoadState::Started);
    ASSERT_EQ(events.back().state, webview::LoadState::Finished);
    ASSERT_NE(events.front().navigationId, 0);
    const auto navigationId = events.front().navigationId;
    bool committed = false;
    for (const auto& event : events) {
        ASSERT_EQ(event.navigationId, navigationId);
        committed = committed || event.state == webview::LoadState::Committed;
        ASSERT_NE(event.state, webview::LoadState::Failed);
    }
    ASSERT_TRUE(committed);
    ASSERT_EQ(messageCount, 1);
    ASSERT_EQ(popupCount, 0);

    const auto eventCount = events.size();
    view->navigate(webview::Url("javascript:window.__policyBypass=true"));
    native_test::runFor(std::chrono::milliseconds(100));
    ASSERT_EQ(events.size(), eventCount);

    const std::string hostilePayload = "</script><script>window.__payloadExecuted=true</script>";
    view->bridge().emitEvent("hello", { { "message", hostilePayload } });
    loop.exec(std::chrono::seconds(10));
    ASSERT_TRUE(hostileReturned);
    ASSERT_EQ(hostileReturnedPayload, hostilePayload);
    ASSERT_FALSE(hostileExecuted);

    const std::string replayHtml = R"HTML(
<!doctype html><script>
window.addEventListener('DOMContentLoaded', () => {
  window.webkit.messageHandlers.systemWebView.postMessage({
    documentToken: 'stale-token',
    message: {version:1,type:'hello',payload:{message:'stale'}}
  });
});
</script>)HTML";
    events.clear();
    view->loadDocument(replayHtml, webview::Url("https://trusted.example/next.html"));
    loop.exec(std::chrono::seconds(10));
    ASSERT_EQ(events.back().state, webview::LoadState::Finished);
    ASSERT_EQ(messageCount, 2);

    ServerStats serverStats;
    native_test::HttpServer server([&](std::string_view request, std::uint16_t port) { return serveConnection(request, port, &serverStats); });
    ASSERT_NE(server.port(), 0);
    const auto urlFor = [&server](const char* path) { return webview::Url("http://127.0.0.1:" + std::to_string(server.port()) + path); };
    events.clear();
    policy->navigationRequests.clear();
    view->navigate(urlFor("/redirect"));
    loop.exec(std::chrono::seconds(10));
    ASSERT_TRUE(!events.empty());
    ASSERT_EQ(events.front().state, webview::LoadState::Started);
    ASSERT_EQ(events.back().state, webview::LoadState::Finished);
    const auto redirectNavigationId = events.front().navigationId;
    bool redirected = false;
    for (const auto& event : events) {
        ASSERT_EQ(event.navigationId, redirectNavigationId);
        redirected = redirected || event.state == webview::LoadState::Redirected;
    }
    ASSERT_TRUE(redirected);
    bool sawInitialRequest = false;
    bool sawRedirectRequest = false;
    for (const auto& request : policy->navigationRequests) {
        if (request.url.path() == "/redirect") {
            sawInitialRequest = true;
            ASSERT_FALSE(request.isRedirect);
        }
        else if (request.url.path() == "/final") {
            sawRedirectRequest = true;
            ASSERT_TRUE(request.isRedirect);
        }
    }
    ASSERT_TRUE(sawInitialRequest);
    ASSERT_TRUE(sawRedirectRequest);

    events.clear();
    view->navigate(urlFor("/reload"));
    loop.exec(std::chrono::seconds(10));
    ASSERT_EQ(events.back().state, webview::LoadState::Finished);
    ASSERT_EQ(serverStats.reloadRequests, 1);
    events.clear();
    view->reload();
    loop.exec(std::chrono::seconds(10));
    ASSERT_EQ(events.back().state, webview::LoadState::Finished);
    ASSERT_EQ(serverStats.reloadRequests, 2);

    events.clear();
    view->navigate(urlFor("/slow"));
    native_test::runFor(std::chrono::milliseconds(100));
    ASSERT_TRUE(!events.empty());
    ASSERT_EQ(events.front().state, webview::LoadState::Started);
    view->stop();
    const auto stoppedEventCount = events.size();
    native_test::runFor(std::chrono::milliseconds(700));
    ASSERT_GE(events.size(), stoppedEventCount);
    ASSERT_TRUE(events.empty() || events.back().state != webview::LoadState::Finished);

    policy->permissionRequests.clear();
    events.clear();
    view->loadDocument(
        R"HTML(
<!doctype html><input id="file" type="file"><script>
window.addEventListener('DOMContentLoaded', () => document.querySelector('#file').click());
</script>)HTML",
        webview::Url("https://trusted.example/file-input.html"));
    loop.exec(std::chrono::seconds(10));
    native_test::runFor(std::chrono::milliseconds(100));
    ASSERT_TRUE(policy->permissionRequests.empty());

    policy->downloadRequests.clear();
    events.clear();
    view->navigate(urlFor("/download"));
    native_test::runFor(std::chrono::milliseconds(500));
    ASSERT_TRUE(!policy->downloadRequests.empty());
    ASSERT_EQ(policy->downloadRequests.back().url.path(), "/download");

    auto raceView = session->createWebView({});
    int raceCallbacks = 0;
    webview::WebViewHostCallbacks raceCallbacksConfig;
    raceCallbacksConfig.onLoad = [&](const webview::LoadEvent&) { ++raceCallbacks; };
    raceView->setHostCallbacks(std::move(raceCallbacksConfig));
    raceView->navigate(urlFor("/slow"));
    raceView->close();
    const auto callbacksAtClose = raceCallbacks;
    native_test::runFor(std::chrono::milliseconds(700));
    ASSERT_TRUE(raceView->isClosed());
    ASSERT_EQ(raceCallbacks, callbacksAtClose);

    server.close();
    native_test::HttpServer unavailableServer([](std::string_view, std::uint16_t) { return std::string(); });
    const auto unavailablePort = unavailableServer.port();
    unavailableServer.close();
    auto failureView = session->createWebView({});
    std::vector<webview::LoadEvent> failureEvents;
    webview::WebViewHostCallbacks failureCallbacks;
    failureCallbacks.onLoad = [&](const webview::LoadEvent& event) {
        failureEvents.push_back(event);
        if (event.state == webview::LoadState::Finished || event.state == webview::LoadState::Failed) {
            loop.quit();
        }
    };
    failureView->setHostCallbacks(std::move(failureCallbacks));
    failureView->navigate(webview::Url("http://127.0.0.1:" + std::to_string(unavailablePort) + "/unavailable"));
    loop.exec(std::chrono::seconds(10));
    ASSERT_TRUE(!failureEvents.empty());
    ASSERT_EQ(failureEvents.front().state, webview::LoadState::Started);
    ASSERT_EQ(failureEvents.back().state, webview::LoadState::Failed);
    ASSERT_TRUE(!failureEvents.back().error.empty());

    webview::WebViewPtr popup;
    NSWindow* popupWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 480)
                                                         styleMask:NSWindowStyleMaskTitled
                                                           backing:NSBackingStoreBuffered
                                                             defer:NO];
    webview::WebViewHost popupHost { popupWindow.contentView, 640, 480, true };
    policy->allowPopups = true;
    webview::WebViewHostCallbacks popupCallbacks;
    popupCallbacks.createPopupHost = [&](const webview::NewWindowRequest&) { return popupHost; };
    popupCallbacks.onNewWindow = [&](const webview::NewWindowRequest&, webview::WebViewPtr child) {
        ASSERT_EQ(child->initializationState(), webview::InitializationState::Ready);
        child->attachNativeView();
        popup = std::move(child);
        loop.quit();
    };
    view->setHostCallbacks(std::move(popupCallbacks));
    view->loadDocument(
        R"HTML(
<!doctype html><script>
window.addEventListener('DOMContentLoaded', () => window.open('https://trusted.example/popup'));
</script>)HTML",
        webview::Url("https://trusted.example/popup-opener.html"));
    loop.exec(std::chrono::seconds(10));
    ASSERT_TRUE(popup);
    ASSERT_FALSE(popup->isClosed());
    popup->attachNativeView();
    session.reset();
    ASSERT_TRUE(view->isClosed());
    ASSERT_TRUE(popup->isClosed());
    const auto navigationRequestCount = policy->navigationRequests.size();
    view->navigate(webview::Url("https://trusted.example/after-session"));
    ASSERT_EQ(policy->navigationRequests.size(), navigationRequestCount);
    bool rootClosed = false;
    bool popupClosed = false;
    view->bridge().call(
        "hello", webview::Json::object(), [&](const webview::Json&, const std::string& error) { rootClosed = !error.empty(); });
    popup->bridge().call(
        "hello", webview::Json::object(), [&](const webview::Json&, const std::string& error) { popupClosed = !error.empty(); });
    ASSERT_TRUE(rootClosed);
    ASSERT_TRUE(popupClosed);

    view->close();
    view->close();
    ASSERT_TRUE(view->isClosed());
    bool closed = false;
    view->bridge().call(
        "hello", { { "message", "late" } }, [&](const webview::Json&, const std::string& error) { closed = !error.empty(); });
    ASSERT_TRUE(closed);
}
