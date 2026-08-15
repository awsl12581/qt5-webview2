#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QTimer>

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
    return 0;
}
