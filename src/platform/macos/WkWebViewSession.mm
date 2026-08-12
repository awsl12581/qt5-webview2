#include "platform/macos/WkWebViewSession.h"

#include "platform/macos/WkWebView.h"

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

WkWebViewSession::~WkWebViewSession() = default;

WebViewPtr WkWebViewSession::createWebView(QWidget* parent)
{
    auto* configuration = static_cast<WKWebViewConfiguration*>(nativeConfigurationForTesting());
    return std::unique_ptr<WkWebView>(new WkWebView(parent, configuration, impl_->policy));
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
} // namespace webview
