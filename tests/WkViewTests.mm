#include "webview/IWebView.h"
#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cassert>
#include <vector>

namespace {
class TestPolicy final : public webview::WebViewPolicy
{
public:
    explicit TestPolicy(webview::WebViewPolicyConfig config)
        : WebViewPolicy(std::move(config))
    {
    }

    webview::NavigationDecision decideNavigation(const webview::NavigationRequest& request) const override
    {
        if (request.url.scheme() == QStringLiteral("http")
            && request.url.host() == QStringLiteral("127.0.0.1")) {
            return webview::NavigationDecision::Allow;
        }
        return WebViewPolicy::decideNavigation(request);
    }
};

void serveConnection(QTcpSocket* socket, quint16 port)
{
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, port] {
        const auto request = socket->readAll();
        QByteArray response;
        if (request.startsWith("GET /redirect ")) {
            response = "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + QByteArray::number(port)
                + "/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else {
            const QByteArray body = "<!doctype html><title>final</title>done";
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        }
        socket->write(response);
        socket->disconnectFromHost();
    });
    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    webview::WebViewPolicyConfig config;
    config.trustedHttpsOrigins.insert(QStringLiteral("https://trusted.example"));
    config.bridgeSchemas.insert(
        QStringLiteral("hello"), { QSet<QString> { QStringLiteral("message") } });
    auto session = webview::createEphemeralSession(std::make_shared<TestPolicy>(std::move(config)));
    auto view = session->createWebView();

    std::vector<webview::LoadEvent> events;
    int messageCount = 0;
    int popupCount = 0;
    bool hostileReturned = false;
    bool hostileExecuted = false;
    QString hostileReturnedPayload;
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
        if (message.payload.contains(QStringLiteral("executed"))) {
            hostileReturned = true;
            hostileExecuted = message.payload.value(QStringLiteral("executed")).toBool();
            hostileReturnedPayload = message.payload.value(QStringLiteral("message")).toString();
            loop.quit();
        } else {
            assert(message.payload.value(QStringLiteral("message")).toString() == QStringLiteral("main"));
        }
    };
    callbacks.newWindow = [&](const webview::NewWindowRequest&, webview::WebViewPtr) { ++popupCount; };
    view->setHostCallbacks(std::move(callbacks));

    const QString html = QStringLiteral(R"HTML(
<!doctype html><html><body>
<iframe srcdoc="<script>window.webkit.messageHandlers.systemWebView.postMessage({version:1,type:'hello',payload:{message:'frame'}})</script>"></iframe>
<script>
window.addEventListener('DOMContentLoaded', () => {
  window.addEventListener('system-webview-message', event => {
    document.body.dataset.nativeMessage = event.detail.payload.message;
    window.webkit.messageHandlers.systemWebView.postMessage({
      version: 1,
      type: 'hello',
      payload: {
        message: event.detail.payload.message,
        executed: window.__payloadExecuted === true
      }
    });
  });
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

    const QString hostilePayload = QStringLiteral("</script><script>window.__payloadExecuted=true</script>");
    bool hostileDelivered = false;
    view->sendMessage({ 1, QStringLiteral("hello"), { { QStringLiteral("message"), hostilePayload } } },
        [&](const webview::MessageResult& result) {
            assert(result.error == webview::MessageError::None);
            hostileDelivered = true;
            if (hostileReturned) {
                loop.quit();
            }
        });
    timeout.start(10000);
    loop.exec();
    assert(hostileDelivered);
    assert(hostileReturned);
    assert(hostileReturnedPayload == hostilePayload);
    assert(!hostileExecuted);

    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            serveConnection(server.nextPendingConnection(), server.serverPort());
        }
    });
    events.clear();
    view->load(QUrl(QStringLiteral("http://127.0.0.1:%1/redirect").arg(server.serverPort())));
    timeout.start(10000);
    loop.exec();
    assert(!events.empty());
    assert(events.front().state == webview::LoadState::Started);
    assert(events.back().state == webview::LoadState::Finished);
    const auto redirectNavigationId = events.front().navigationId;
    bool redirected = false;
    for (const auto& event : events) {
        assert(event.navigationId == redirectNavigationId);
        redirected = redirected || event.state == webview::LoadState::Redirected;
    }
    assert(redirected);

    const auto closedPort = server.serverPort();
    server.close();
    events.clear();
    view->load(QUrl(QStringLiteral("http://127.0.0.1:%1/unavailable").arg(closedPort)));
    timeout.start(10000);
    loop.exec();
    assert(!events.empty());
    assert(events.front().state == webview::LoadState::Started);
    assert(events.back().state == webview::LoadState::Failed);
    assert(!events.back().error.isEmpty());

    view->close();
    view->close();
    assert(view->isClosed());
    bool closed = false;
    view->sendMessage({ 1, QStringLiteral("hello"), { { QStringLiteral("message"), QStringLiteral("late") } } },
        [&](const webview::MessageResult& result) { closed = result.error == webview::MessageError::Closed; });
    assert(closed);
}
