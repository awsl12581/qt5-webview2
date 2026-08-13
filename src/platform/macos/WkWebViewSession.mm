#include "platform/macos/WkWebViewSession.h"

#include "platform/macos/WkWebView.h"
#include "platform/macos/WkSessionState.h"

#import <WebKit/WebKit.h>

namespace webview
{
class WkWebViewSession::Impl
{
public:
    QString profilePath;
    bool ephemeral = false;
    WebViewPolicyPtr policy;
    WKWebsiteDataStore* dataStore = nil;
    WKProcessPool* processPool = nil;
    std::shared_ptr<WkSessionState> state = std::make_shared<WkSessionState>();
};

WkWebViewSession::WkWebViewSession(QString profilePath, bool ephemeral, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>())
{
    impl_->profilePath = std::move(profilePath);
    impl_->ephemeral = ephemeral;
    impl_->policy = policy ? std::move(policy) : createDefaultWebViewPolicy();
    impl_->dataStore = ephemeral ? [WKWebsiteDataStore nonPersistentDataStore] : [WKWebsiteDataStore defaultDataStore];
    impl_->processPool = [[WKProcessPool alloc] init];
}

WkWebViewSession::~WkWebViewSession()
{
    impl_->state->valid = false;
    const auto views = impl_->state->views;
    for (auto* view : views) {
        view->close();
    }
    impl_->state->views.clear();
    impl_->policy.reset();
}

WebViewPtr WkWebViewSession::createWebView(QWidget* parent)
{
    auto* configuration = static_cast<WKWebViewConfiguration*>(nativeConfigurationForTesting());
    return std::unique_ptr<WkWebView>(new WkWebView(parent, configuration, impl_->policy, impl_->state));
}

void* WkWebViewSession::nativeConfigurationForTesting() const
{
    auto* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = impl_->dataStore;
    configuration.processPool = impl_->processPool;
    return static_cast<void*>(configuration);
}

namespace {
void clearData(WKWebsiteDataStore* store, NSSet<NSString*>* types, IWebViewSession::ClearCompletion completion)
{
    [store removeDataOfTypes:types
               modifiedSince:[NSDate distantPast]
           completionHandler:^{
               if (completion) {
                   completion({ });
               }
           }];
}
}

void WkWebViewSession::clearCache(ClearCompletion completion)
{
    clearData(impl_->dataStore,
        [NSSet setWithObjects:WKWebsiteDataTypeDiskCache, WKWebsiteDataTypeMemoryCache, nil],
        std::move(completion));
}

void WkWebViewSession::clearCookies(ClearCompletion completion)
{
    clearData(impl_->dataStore, [NSSet setWithObject:WKWebsiteDataTypeCookies], std::move(completion));
}

void WkWebViewSession::clearWebsiteData(ClearCompletion completion)
{
    clearData(impl_->dataStore, [WKWebsiteDataStore allWebsiteDataTypes], std::move(completion));
}

CapabilitySupport WkWebViewSession::permissionSupport(PermissionKind kind) const
{
    switch (kind) {
    case PermissionKind::Camera:
    case PermissionKind::Microphone:
        if (@available(macOS 12.0, *)) {
            return CapabilitySupport::Supported;
        }
        return CapabilitySupport::Unsupported;
    case PermissionKind::FilePicker:
        return CapabilitySupport::Supported;
    case PermissionKind::Location:
    case PermissionKind::Notifications:
    case PermissionKind::Clipboard:
        return CapabilitySupport::Unsupported;
    }
    return CapabilitySupport::Unsupported;
}

CapabilitySupport WkWebViewSession::downloadSupport() const
{
    if (@available(macOS 11.3, *)) {
        return CapabilitySupport::Supported;
    }
    return CapabilitySupport::Unsupported;
}
} // namespace webview
