#include "platform/macos/WkWebViewSession.h"

#include "platform/macos/WkWebView.h"
#include "platform/macos/WkSessionState.h"

#import <WebKit/WebKit.h>

namespace webview
{
class WkWebViewSession::Impl
{
public:
    WebViewSessionOptions options;
    WebViewPolicyPtr policy;
    WKWebsiteDataStore* dataStore = nil;
    WKProcessPool* processPool = nil;
    std::shared_ptr<WkSessionState> state = std::make_shared<WkSessionState>();
};

WkWebViewSession::WkWebViewSession(WebViewSessionOptions options, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>())
{
    impl_->options = std::move(options);
    impl_->policy = policy ? std::move(policy) : createDefaultWebViewPolicy();
    impl_->dataStore = impl_->options.mode == SessionMode::Ephemeral
        ? [WKWebsiteDataStore nonPersistentDataStore]
        : [WKWebsiteDataStore defaultDataStore];
    impl_->processPool = [[WKProcessPool alloc] init];
    impl_->state->resourceMappings = impl_->options.resourceMappings;
}

InitializationState WkWebViewSession::initializationState() const
{
    return impl_->state->valid ? InitializationState::Ready : InitializationState::Closed;
}

void WkWebViewSession::whenInitialized(InitializationCompletion completion)
{
    if (completion) {
        completion({ initializationState(), { } });
    }
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
    auto* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = impl_->dataStore;
    configuration.processPool = impl_->processPool;
    return std::unique_ptr<WkWebView>(new WkWebView(parent, configuration, impl_->policy, impl_->state));
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

CapabilitySupport WkWebViewSession::capabilitySupport(WebViewCapability capability) const
{
    switch (capability) {
    case WebViewCapability::PersistentProfile:
        return impl_->options.mode == SessionMode::Persistent
            ? CapabilitySupport::Supported : CapabilitySupport::Unsupported;
    case WebViewCapability::PrivateProfile:
        return impl_->options.mode == SessionMode::Ephemeral
            ? CapabilitySupport::Supported : CapabilitySupport::Unsupported;
    case WebViewCapability::FileSelection:
        return CapabilitySupport::Supported;
    case WebViewCapability::ResourceMapping:
        return impl_->options.resourceMappings.isEmpty()
            ? CapabilitySupport::Unsupported
            : CapabilitySupport::Supported;
    case WebViewCapability::DownloadDefault:
        if (@available(macOS 11.3, *)) {
            return CapabilitySupport::Supported;
        }
        return CapabilitySupport::Unsupported;
    case WebViewCapability::DownloadTarget:
        return CapabilitySupport::Unsupported;
    case WebViewCapability::Camera:
    case WebViewCapability::Microphone:
        if (@available(macOS 12.0, *)) {
            return CapabilitySupport::Supported;
        }
        return CapabilitySupport::Unsupported;
    case WebViewCapability::Location:
    case WebViewCapability::Notifications:
    case WebViewCapability::Clipboard:
        return CapabilitySupport::Unsupported;
    }
    return CapabilitySupport::Unsupported;
}
} // namespace webview
