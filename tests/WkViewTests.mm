#include "webview/IWebView.h"
#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QEventLoop>
#include <QVBoxLayout>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

#include <cassert>
#include <memory>
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
        navigationRequests.push_back(request);
        if (request.url.scheme() == QStringLiteral("http")
            && request.url.host() == QStringLiteral("127.0.0.1")) {
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

struct ServerStats {
    int reloadRequests = 0;
    int slowRequests = 0;
};

void serveConnection(QTcpSocket* socket, quint16 port, ServerStats* stats)
{
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, port, stats] {
        const auto request = socket->readAll();
        QByteArray response;
        if (request.startsWith("GET /redirect ")) {
            response = "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + QByteArray::number(port)
                + "/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else if (request.startsWith("GET /download ")) {
            const QByteArray body = "download-data";
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                       "Content-Disposition: attachment; filename=test.bin\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        } else if (request.startsWith("GET /slow ")) {
            ++stats->slowRequests;
            QTimer::singleShot(500, socket, [socket] {
                if (socket->state() == QAbstractSocket::ConnectedState) {
                    const QByteArray body = "<!doctype html><title>slow</title>done";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                }
            });
            return;
        } else {
            if (request.startsWith("GET /reload ")) {
                ++stats->reloadRequests;
            }
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
    auto policy = std::make_shared<TestPolicy>(std::move(config));
    auto session = webview::createWebViewSession({ }, policy);
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
    callbacks.newWindow = [&](const webview::NewWindowRequest&, webview::WebViewPtr) {
        ++popupCount;
    };
    view->setHostCallbacks(std::move(callbacks));

    const QString html = QStringLiteral(R"HTML(
<!doctype html><html><body>
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
  window.systemWebView.postMessage({version:1,type:'hello',payload:{message:'main'}});
  window.open('https://trusted.example/popup');
});
</script>
</body></html>)HTML");
    view->loadDocument(html, QUrl(QStringLiteral("https://trusted.example/index.html")));
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
    view->navigate(QUrl(QStringLiteral("javascript:window.__policyBypass=true")));
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

    const QString replayHtml = QStringLiteral(R"HTML(
<!doctype html><script>
window.addEventListener('DOMContentLoaded', () => {
  window.webkit.messageHandlers.systemWebView.postMessage({
    documentToken: 'stale-token',
    message: {version:1,type:'hello',payload:{message:'stale'}}
  });
});
</script>)HTML");
    events.clear();
    view->loadDocument(replayHtml, QUrl(QStringLiteral("https://trusted.example/next.html")));
    timeout.start(10000);
    loop.exec();
    assert(events.back().state == webview::LoadState::Finished);
    assert(messageCount == 2);

    QTcpServer server;
    assert(server.listen(QHostAddress::LocalHost, 0));
    ServerStats serverStats;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            serveConnection(server.nextPendingConnection(), server.serverPort(), &serverStats);
        }
    });
    events.clear();
    policy->navigationRequests.clear();
    view->navigate(QUrl(QStringLiteral("http://127.0.0.1:%1/redirect").arg(server.serverPort())));
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
    bool sawInitialRequest = false;
    bool sawRedirectRequest = false;
    for (const auto& request : policy->navigationRequests) {
        if (request.url.path() == QStringLiteral("/redirect")) {
            sawInitialRequest = true;
            assert(!request.isRedirect);
        } else if (request.url.path() == QStringLiteral("/final")) {
            sawRedirectRequest = true;
            assert(request.isRedirect);
        }
    }
    assert(sawInitialRequest);
    assert(sawRedirectRequest);

    events.clear();
    view->navigate(QUrl(QStringLiteral("http://127.0.0.1:%1/reload").arg(server.serverPort())));
    timeout.start(10000);
    loop.exec();
    assert(events.back().state == webview::LoadState::Finished);
    assert(serverStats.reloadRequests == 1);
    events.clear();
    view->reload();
    timeout.start(10000);
    loop.exec();
    assert(events.back().state == webview::LoadState::Finished);
    assert(serverStats.reloadRequests == 2);

    events.clear();
    view->navigate(QUrl(QStringLiteral("http://127.0.0.1:%1/slow").arg(server.serverPort())));
    QEventLoop startedLoop;
    QTimer::singleShot(100, &startedLoop, &QEventLoop::quit);
    startedLoop.exec();
    assert(!events.empty());
    assert(events.front().state == webview::LoadState::Started);
    view->stop();
    const auto stoppedEventCount = events.size();
    QEventLoop stoppedLoop;
    QTimer::singleShot(700, &stoppedLoop, &QEventLoop::quit);
    stoppedLoop.exec();
    assert(events.size() >= stoppedEventCount);
    assert(events.empty() || events.back().state != webview::LoadState::Finished);

    policy->permissionRequests.clear();
    events.clear();
    view->loadDocument(QStringLiteral(R"HTML(
<!doctype html><input id="file" type="file"><script>
window.addEventListener('DOMContentLoaded', () => document.querySelector('#file').click());
</script>)HTML"), QUrl(QStringLiteral("https://trusted.example/file-input.html")));
    timeout.start(10000);
    loop.exec();
    QEventLoop permissionLoop;
    QTimer::singleShot(100, &permissionLoop, &QEventLoop::quit);
    permissionLoop.exec();
    assert(policy->permissionRequests.empty());

    policy->downloadRequests.clear();
    events.clear();
    view->navigate(QUrl(QStringLiteral("http://127.0.0.1:%1/download").arg(server.serverPort())));
    QEventLoop downloadLoop;
    QTimer::singleShot(500, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();
    assert(!policy->downloadRequests.empty());
    assert(policy->downloadRequests.back().url.path() == QStringLiteral("/download"));

    auto raceView = session->createWebView();
    int raceCallbacks = 0;
    webview::WebViewHostCallbacks raceCallbacksConfig;
    raceCallbacksConfig.load = [&](const webview::LoadEvent&) { ++raceCallbacks; };
    raceView->setHostCallbacks(std::move(raceCallbacksConfig));
    raceView->navigate(QUrl(QStringLiteral("http://127.0.0.1:%1/slow").arg(server.serverPort())));
    raceView->close();
    const auto callbacksAtClose = raceCallbacks;
    QEventLoop raceLoop;
    QTimer::singleShot(700, &raceLoop, &QEventLoop::quit);
    raceLoop.exec();
    assert(raceView->isClosed());
    assert(raceCallbacks == callbacksAtClose);

    server.close();
    QTcpServer unavailableServer;
    assert(unavailableServer.listen(QHostAddress::LocalHost, 0));
    const auto unavailablePort = unavailableServer.serverPort();
    unavailableServer.close();
    auto failureView = session->createWebView();
    std::vector<webview::LoadEvent> failureEvents;
    webview::WebViewHostCallbacks failureCallbacks;
    failureCallbacks.load = [&](const webview::LoadEvent& event) {
        failureEvents.push_back(event);
        if (event.state == webview::LoadState::Finished || event.state == webview::LoadState::Failed) {
            loop.quit();
        }
    };
    failureView->setHostCallbacks(std::move(failureCallbacks));
    failureView->navigate(QUrl(QStringLiteral("http://127.0.0.1:%1/unavailable").arg(unavailablePort)));
    timeout.start(10000);
    loop.exec();
    assert(!failureEvents.empty());
    assert(failureEvents.front().state == webview::LoadState::Started);
    assert(failureEvents.back().state == webview::LoadState::Failed);
    assert(!failureEvents.back().error.isEmpty());

    QWidget popupHost;
    popupHost.resize(640, 480);
    popupHost.show();
    webview::WebViewPtr popup;
    policy->allowPopups = true;
    webview::WebViewHostCallbacks popupCallbacks;
    popupCallbacks.newWindow = [&](const webview::NewWindowRequest&, webview::WebViewPtr child) {
        assert(child->initializationState() == webview::InitializationState::Ready);
        auto* layout = new QVBoxLayout(&popupHost);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(child->widget());
        popupHost.layout()->activate();
        child->attachNativeView();
        popup = std::move(child);
        loop.quit();
    };
    view->setHostCallbacks(std::move(popupCallbacks));
    view->loadDocument(QStringLiteral(R"HTML(
<!doctype html><script>
window.addEventListener('DOMContentLoaded', () => window.open('https://trusted.example/popup'));
</script>)HTML"), QUrl(QStringLiteral("https://trusted.example/popup-opener.html")));
    timeout.start(10000);
    loop.exec();
    assert(popup);
    assert(!popup->isClosed());
    popup->attachNativeView();
    session.reset();
    assert(view->isClosed());
    assert(popup->isClosed());
    const auto navigationRequestCount = policy->navigationRequests.size();
    view->navigate(QUrl(QStringLiteral("https://trusted.example/after-session")));
    assert(policy->navigationRequests.size() == navigationRequestCount);
    bool rootClosed = false;
    bool popupClosed = false;
    view->sendMessage({ 1, QStringLiteral("hello"), { } },
        [&](const webview::MessageResult& result) { rootClosed = result.error == webview::MessageError::Closed; });
    popup->sendMessage({ 1, QStringLiteral("hello"), { } },
        [&](const webview::MessageResult& result) { popupClosed = result.error == webview::MessageError::Closed; });
    assert(rootClosed);
    assert(popupClosed);

    view->close();
    view->close();
    assert(view->isClosed());
    bool closed = false;
    view->sendMessage({ 1, QStringLiteral("hello"), { { QStringLiteral("message"), QStringLiteral("late") } } },
        [&](const webview::MessageResult& result) { closed = result.error == webview::MessageError::Closed; });
    assert(closed);
}
