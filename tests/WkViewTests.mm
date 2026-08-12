#include "webview/IWebView.h"
#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QEventLoop>
#include <QTimer>

#include <cassert>
#include <vector>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    webview::WebViewPolicyConfig config;
    config.trustedHttpsOrigins.insert(QStringLiteral("https://trusted.example"));
    config.bridgeSchemas.insert(
        QStringLiteral("hello"), { QSet<QString> { QStringLiteral("message") } });
    auto session = webview::createEphemeralSession(webview::createDefaultWebViewPolicy(std::move(config)));
    auto view = session->createWebView();

    std::vector<webview::LoadEvent> events;
    int messageCount = 0;
    int popupCount = 0;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    webview::WebViewHostCallbacks callbacks;
    callbacks.load = [&](const webview::LoadEvent& event) {
        events.push_back(event);
        if (event.state == webview::LoadState::Finished || event.state == webview::LoadState::Failed) {
            loop.quit();
        }
    };
    callbacks.message = [&](const webview::BridgeMessage& message) {
        ++messageCount;
        assert(message.type == QStringLiteral("hello"));
        assert(message.payload.value(QStringLiteral("message")).toString() == QStringLiteral("main"));
    };
    callbacks.newWindow = [&](const webview::NewWindowRequest&, webview::WebViewPtr) { ++popupCount; };
    view->setHostCallbacks(std::move(callbacks));

    const QString html = QStringLiteral(R"HTML(
<!doctype html><html><body>
<iframe srcdoc="<script>window.webkit.messageHandlers.systemWebView.postMessage({version:1,type:'hello',payload:{message:'frame'}})</script>"></iframe>
<script>
window.addEventListener('DOMContentLoaded', () => {
  window.webkit.messageHandlers.systemWebView.postMessage({version:1,type:'hello',payload:{message:'main'}});
  window.open('https://trusted.example/popup');
});
</script>
</body></html>)HTML");
    view->setHtml(html, QUrl(QStringLiteral("https://trusted.example/index.html")));
    timeout.start(10000);
    loop.exec();

    assert(!events.empty());
    assert(events.front().state == webview::LoadState::Started);
    assert(events.back().state == webview::LoadState::Finished);
    assert(events.front().navigationId != 0);
    const auto navigationId = events.front().navigationId;
    bool committed = false;
    for (const auto& event : events) {
        assert(event.navigationId == navigationId);
        committed = committed || event.state == webview::LoadState::Committed;
        assert(event.state != webview::LoadState::Failed);
    }
    assert(committed);
    assert(messageCount == 1);
    assert(popupCount == 0);

    const auto eventCount = events.size();
    view->load(QUrl(QStringLiteral("javascript:window.__policyBypass=true")));
    QEventLoop rejectionLoop;
    QTimer::singleShot(100, &rejectionLoop, &QEventLoop::quit);
    rejectionLoop.exec();
    assert(events.size() == eventCount);

    bool rejected = false;
    view->sendMessage({ 1, QStringLiteral("unknown"), { } }, [&](const webview::MessageResult& result) {
        rejected = result.error == webview::MessageError::Rejected;
    });
    assert(rejected);

    view->close();
    view->close();
    assert(view->isClosed());
    bool closed = false;
    view->sendMessage({ 1, QStringLiteral("hello"), { { QStringLiteral("message"), QStringLiteral("late") } } },
        [&](const webview::MessageResult& result) { closed = result.error == webview::MessageError::Closed; });
    assert(closed);
}
