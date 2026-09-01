#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cassert>

namespace {
bool waitForClear(const std::function<void(webview::IWebViewSession::ClearCompletion)>& clear)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool completed = false;
    bool succeeded = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    clear([&](const webview::WebsiteDataResult& result) {
        completed = true;
        succeeded = result.success;
        loop.quit();
    });
    timeout.start(15000);
    loop.exec();
    return completed && succeeded;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QTemporaryDir profile;
    assert(profile.isValid());

    webview::WebViewSessionOptions persistentOptions;
    persistentOptions.mode = webview::SessionMode::Persistent;
    persistentOptions.profilePath = profile.path();
    auto session = webview::createWebViewSession(std::move(persistentOptions));
    auto ephemeral = webview::createWebViewSession({ });
    webview::WebApplicationOptions invalidBundle;
    invalidBundle.id = QStringLiteral("demo");
    invalidBundle.source = webview::LocalBundle { profile.path(), QStringLiteral("../index.html") };
    assert(!session->createApplication(std::move(invalidBundle)));
    QFile bundleEntry(profile.filePath(QStringLiteral("index.html")));
    assert(bundleEntry.open(QIODevice::WriteOnly));
    bundleEntry.close();
    webview::WebApplicationOptions bundle;
    bundle.id = QStringLiteral("bundle");
    bundle.source = webview::LocalBundle { profile.path() };
    assert(session->createApplication(bundle));
    assert(!session->createApplication(std::move(bundle)));
    assert(session->capabilitySupport(webview::WebViewCapability::PersistentProfile)
        == webview::CapabilitySupport::Supported);
    assert(ephemeral->capabilitySupport(webview::WebViewCapability::PersistentProfile)
        == webview::CapabilitySupport::Supported);
    assert(ephemeral->capabilitySupport(webview::WebViewCapability::PrivateProfile)
        == webview::CapabilitySupport::Supported);
    assert(session->capabilitySupport(webview::WebViewCapability::PrivateProfile)
        == webview::CapabilitySupport::Supported);
    assert(session->capabilitySupport(webview::WebViewCapability::FileSelection)
        == webview::CapabilitySupport::Supported);
    assert(session->capabilitySupport(webview::WebViewCapability::Location)
        == webview::CapabilitySupport::Unsupported);
    assert(session->capabilitySupport(webview::WebViewCapability::Notifications)
        == webview::CapabilitySupport::Unsupported);
    assert(session->capabilitySupport(webview::WebViewCapability::Clipboard)
        == webview::CapabilitySupport::Unsupported);
    if (@available(macOS 12.0, *)) {
        assert(session->capabilitySupport(webview::WebViewCapability::Camera)
            == webview::CapabilitySupport::Supported);
        assert(session->capabilitySupport(webview::WebViewCapability::Microphone)
            == webview::CapabilitySupport::Supported);
    }
    if (@available(macOS 11.3, *)) {
        assert(session->capabilitySupport(webview::WebViewCapability::DownloadTarget)
            == webview::CapabilitySupport::Supported);
    }

    assert(waitForClear([&](auto completion) { session->clearCache(std::move(completion)); }));
    assert(waitForClear([&](auto completion) { session->clearCookies(std::move(completion)); }));
    assert(waitForClear([&](auto completion) { session->clearWebsiteData(std::move(completion)); }));

}
