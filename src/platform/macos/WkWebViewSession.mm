#include "platform/macos/WkWebViewSession.h"

#include "platform/macos/WkWebView.h"
#include "platform/macos/WkSessionState.h"

#include "internal/ResourceMapping.h"
#include "internal/Application.h"

#include <QFile>
#include <QMimeDatabase>
#include <QSet>

#import <WebKit/WebKit.h>

@interface SystemWebViewSchemeHandler : NSObject <WKURLSchemeHandler> {
@public
    std::shared_ptr<webview::WkSessionState> state;
}
@end

@implementation SystemWebViewSchemeHandler
- (void)webView:(WKWebView*)webView
    startURLSchemeTask:(id<WKURLSchemeTask>)task
{
    if (!state || !state->valid) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                   code:NSURLErrorCancelled
                                               userInfo:nil]];
        return;
    }
    const QUrl url(QString::fromUtf8(task.request.URL.absoluteString.UTF8String));
    const auto* mapping = webview::findResourceMapping(state->resourceMappings, url);
    QString errorText;
    const auto accept = [task.request valueForHTTPHeaderField:@"Accept"];
    const bool mainDocumentRequest = accept && [accept containsString:@"text/html"];
    const auto path = mapping
        ? webview::resolveMappedResource(*mapping, url, &errorText, mainDocumentRequest) : QString();
    if (path.isEmpty()) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                   code:NSURLErrorFileDoesNotExist
                                               userInfo:@{ NSLocalizedDescriptionKey :
                                                   [NSString stringWithUTF8String:errorText.toUtf8().constData()] }]];
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
                                                   code:NSURLErrorNoPermissionsToReadFile
                                               userInfo:nil]];
        return;
    }
    const auto bytes = file.readAll();
    const auto mimeType = QMimeDatabase().mimeTypeForFile(path).name();
    auto* response = [[NSURLResponse alloc] initWithURL:task.request.URL
                                              MIMEType:[NSString stringWithUTF8String:mimeType.toUtf8().constData()]
                                 expectedContentLength:bytes.size()
                                      textEncodingName:nil];
    [task didReceiveResponse:response];
    [task didReceiveData:[NSData dataWithBytes:bytes.constData() length:bytes.size()]];
    [task didFinish];
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task
{
}
@end

namespace webview
{
class WkWebViewSession::Impl
{
public:
    WebViewSessionOptions options;
    WebViewPolicyPtr policy;
    WKWebsiteDataStore* dataStore = nil;
    WKProcessPool* processPool = nil;
    id schemeHandler = nil;
    std::shared_ptr<WkSessionState> state = std::make_shared<WkSessionState>();
    QSet<QString> applicationIds;
    QVector<WebApplicationPtr> applications;
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
    auto* schemeHandler = [[SystemWebViewSchemeHandler alloc] init];
    schemeHandler->state = impl_->state;
    impl_->schemeHandler = schemeHandler;
    impl_->state->initialization.markReady();
}

InitializationState WkWebViewSession::initializationState() const
{
    return impl_->state->initialization.state();
}

void WkWebViewSession::whenInitialized(InitializationCompletion completion)
{
    if (completion) {
        impl_->state->initialization.whenInitialized(std::move(completion));
    }
}

WebApplicationPtr WkWebViewSession::createApplication(WebApplicationOptions options)
{
    QString error;
    const auto application = webview::createApplication(std::move(options), &error);
    if (!application || impl_->applicationIds.contains(application->id())) return { };
    if (const auto* bundle = std::get_if<LocalBundle>(&application->source())) {
        ResourceMapping mapping { application->origin(), bundle->directory, bundle->entryDocument, bundle->spaFallback };
        QVector<ResourceMapping> candidate = impl_->state->resourceMappings;
        candidate.push_back(std::move(mapping));
        if (!validateResourceMappings(&candidate, &error)
            || resolveMappedResource(candidate.back(), application->urlForRoute({ }), &error).isEmpty()) return { };
        impl_->state->resourceMappings = std::move(candidate);
    }
    impl_->applicationIds.insert(application->id());
    impl_->applications.push_back(application);
    return application;
}

WkWebViewSession::~WkWebViewSession()
{
    impl_->state->valid = false;
    impl_->state->initialization.close();
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
    if (impl_->schemeHandler) {
        [configuration setURLSchemeHandler:impl_->schemeHandler forURLScheme:@"app"];
    }
    return std::unique_ptr<WkWebView>(new WkWebView(parent, configuration, impl_->policy, impl_->state));
}

namespace {
void clearData(WkSessionState& state, WKWebsiteDataStore* store, NSSet<NSString*>* types,
    IWebViewSession::ClearCompletion completion)
{
    state.initialization.runWhenReady(
        [store, types, completion = std::move(completion)](const InitializationResult& result) {
            if (result.state != InitializationState::Ready) {
                if (completion) {
                    completion({ false, result.error });
                }
                return;
            }
            [store removeDataOfTypes:types
                       modifiedSince:[NSDate distantPast]
                   completionHandler:^{
                       if (completion) {
                           completion({ });
                       }
                   }];
        });
}
}

void WkWebViewSession::clearCache(ClearCompletion completion)
{
    clearData(*impl_->state, impl_->dataStore,
        [NSSet setWithObjects:WKWebsiteDataTypeDiskCache, WKWebsiteDataTypeMemoryCache, nil],
        std::move(completion));
}

void WkWebViewSession::clearCookies(ClearCompletion completion)
{
    clearData(*impl_->state, impl_->dataStore, [NSSet setWithObject:WKWebsiteDataTypeCookies],
        std::move(completion));
}

void WkWebViewSession::clearWebsiteData(ClearCompletion completion)
{
    clearData(*impl_->state, impl_->dataStore, [WKWebsiteDataStore allWebsiteDataTypes],
        std::move(completion));
}

CapabilitySupport WkWebViewSession::capabilitySupport(WebViewCapability capability) const
{
    switch (capability) {
    case WebViewCapability::PersistentProfile:
        return CapabilitySupport::Supported;
    case WebViewCapability::PrivateProfile:
        return CapabilitySupport::Supported;
    case WebViewCapability::FileSelection:
        return CapabilitySupport::Supported;
    case WebViewCapability::DownloadDefault:
        return CapabilitySupport::Unsupported;
    case WebViewCapability::DownloadTarget:
        if (@available(macOS 11.3, *)) {
            return CapabilitySupport::Supported;
        }
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
