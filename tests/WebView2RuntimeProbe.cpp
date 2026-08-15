#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include "webview/IWebView.h"

#include <cassert>
#include <cstdio>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir profile;
    assert(profile.isValid());
    webview::WebViewSessionOptions options;
    options.mode = webview::SessionMode::Persistent;
    options.profilePath = profile.path();
    auto session = webview::createWebViewSession(std::move(options));
    webview::InitializationResult result;
    bool completed = false;
    session->whenInitialized([&](const webview::InitializationResult& value) {
        result = value;
        completed = true;
        QCoreApplication::quit();
    });
    QTimer::singleShot(15000, &app, &QCoreApplication::quit);
    app.exec();
    assert(completed);
    assert(result.state == webview::InitializationState::Ready
        || result.state == webview::InitializationState::Failed);
    std::printf("state=%d error=%s\\n", static_cast<int>(result.state), result.error.toUtf8().constData());
    if (result.state == webview::InitializationState::Failed) {
        assert(!result.error.isEmpty());
    }
    std::fflush(stdout);
    if (result.state == webview::InitializationState::Ready) {
        QWidget host;
        host.resize(640, 480);
        auto view = session->createWebView(&host);
        webview::InitializationResult viewResult;
        bool viewCompleted = false;
        view->whenInitialized([&](const webview::InitializationResult& value) {
            viewResult = value;
            viewCompleted = true;
            std::printf("view_callback state=%d error=%s\\n", static_cast<int>(value.state), value.error.toUtf8().constData());
            std::fflush(stdout);
            QCoreApplication::quit();
        });
        host.show();
        QTimer::singleShot(15000, &app, [&] {
            std::printf("view_timeout\\n");
            std::fflush(stdout);
            QCoreApplication::quit();
        });
        app.exec();
        if (viewCompleted) {
            view->attachNativeView();
            view->detachNativeView();
        }
        view->close();
    }
    return 0;
}
