#include "platform/macos/WkWebViewSession.h"
#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#import <WebKit/WebKit.h>

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

    auto session = webview::createPersistentSession(profile.path());
    auto* nativeSession = static_cast<webview::WkWebViewSession*>(session.get());
    auto* firstConfiguration = static_cast<WKWebViewConfiguration*>(nativeSession->nativeConfigurationForTesting());
    auto* secondConfiguration = static_cast<WKWebViewConfiguration*>(nativeSession->nativeConfigurationForTesting());
    assert(firstConfiguration.websiteDataStore == secondConfiguration.websiteDataStore);
    assert(firstConfiguration.processPool == secondConfiguration.processPool);
    assert(firstConfiguration.userContentController != secondConfiguration.userContentController);
    assert(firstConfiguration.websiteDataStore == [WKWebsiteDataStore defaultDataStore]);

    auto ephemeral = webview::createEphemeralSession();
    auto* privateConfiguration = static_cast<WKWebViewConfiguration*>(
        static_cast<webview::WkWebViewSession*>(ephemeral.get())->nativeConfigurationForTesting());
    assert(!privateConfiguration.websiteDataStore.persistent);
    assert(privateConfiguration.websiteDataStore != [WKWebsiteDataStore defaultDataStore]);
    assert(session->permissionSupport(webview::PermissionKind::FilePicker)
        == webview::CapabilitySupport::Supported);
    assert(session->permissionSupport(webview::PermissionKind::Location)
        == webview::CapabilitySupport::Unsupported);
    assert(session->permissionSupport(webview::PermissionKind::Notifications)
        == webview::CapabilitySupport::Unsupported);
    assert(session->permissionSupport(webview::PermissionKind::Clipboard)
        == webview::CapabilitySupport::Unsupported);
    if (@available(macOS 12.0, *)) {
        assert(session->permissionSupport(webview::PermissionKind::Camera)
            == webview::CapabilitySupport::Supported);
        assert(session->permissionSupport(webview::PermissionKind::Microphone)
            == webview::CapabilitySupport::Supported);
    }
    if (@available(macOS 11.3, *)) {
        assert(session->downloadSupport() == webview::CapabilitySupport::Supported);
    }

    assert(waitForClear([&](auto completion) { session->clearCache(std::move(completion)); }));
    assert(waitForClear([&](auto completion) { session->clearCookies(std::move(completion)); }));
    assert(waitForClear([&](auto completion) { session->clearWebsiteData(std::move(completion)); }));

}
