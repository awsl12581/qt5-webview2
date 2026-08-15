#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QEventLoop>
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
    webview::WebViewSessionOptions invalidMappingOptions;
    invalidMappingOptions.resourceMappings.push_back(
        { QUrl(QStringLiteral("app://demo/path")), profile.path() });
    auto invalidMappingSession = webview::createWebViewSession(std::move(invalidMappingOptions));
    assert(invalidMappingSession->initializationState() == webview::InitializationState::Failed);
    bool mappingFailureReported = false;
    invalidMappingSession->whenInitialized([&](const webview::InitializationResult& result) {
        mappingFailureReported = result.state == webview::InitializationState::Failed
            && result.error.contains(QStringLiteral("origin"));
    });
    assert(mappingFailureReported);
    assert(session->capabilitySupport(webview::WebViewCapability::PersistentProfile)
        == webview::CapabilitySupport::Supported);
    assert(ephemeral->capabilitySupport(webview::WebViewCapability::PrivateProfile)
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
        assert(session->capabilitySupport(webview::WebViewCapability::DownloadDefault)
            == webview::CapabilitySupport::Supported);
    }

    assert(waitForClear([&](auto completion) { session->clearCache(std::move(completion)); }));
    assert(waitForClear([&](auto completion) { session->clearCookies(std::move(completion)); }));
    assert(waitForClear([&](auto completion) { session->clearWebsiteData(std::move(completion)); }));

}
