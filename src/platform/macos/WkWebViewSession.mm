#include "platform/macos/WkWebViewSession.h"

#include "platform/macos/WkSessionState.h"
#include "platform/macos/WkWebView.h"

#include "internal/Application.h"
#include "internal/Diagnostics.h"
#include "internal/HttpStatus.h"
#include "internal/ResourceMapping.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>

#include <vector>

#import <WebKit/WebKit.h>

@interface SystemWebViewSchemeHandler : NSObject <WKURLSchemeHandler> {
@public
    std::shared_ptr<webview::WkSessionState> state;
    NSMutableSet<NSValue*>* cancelledTasks;
}
@end

@implementation SystemWebViewSchemeHandler
- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)task
{
    @synchronized(self) {
        [cancelledTasks removeObject:[NSValue valueWithNonretainedObject:task]];
    }
    if (!state || !state->valid) {
        [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCancelled userInfo:nil]];
        return;
    }
    const QUrl url(QString::fromUtf8(task.request.URL.absoluteString.UTF8String));
    const QUrl documentUrl(QString::fromUtf8(task.request.mainDocumentURL.absoluteString.UTF8String));
    webview::WebViewState* pageState = nullptr;
    std::shared_ptr<webview::WebViewState> pageStateOwner;
    {
        std::lock_guard<std::mutex> lock(state->viewsMutex);
        for (auto* view : state->views) {
            if (view->ownsNativeView(static_cast<void*>(webView))) {
                pageStateOwner = view->stateForHostCompletion();
                pageState = pageStateOwner.get();
                break;
            }
        }
    }

    webview::ResourceResponse resource;
    const bool published = url.path().startsWith(QStringLiteral("/resource/"));
    if (published && pageState) {
        const auto range = [task.request valueForHTTPHeaderField:@"Range"];
        resource = pageState->resources->open(
            { url, documentUrl, pageState->documentToken, range ? QString::fromUtf8(range.UTF8String) : QString() });
    }

    QString path;
    QString mimeType;
    int status = published ? resource.status : webview::httpStatus::kOk;
    qint64 totalSize = resource.totalSize;
    qint64 offset = resource.offset;
    qint64 length = resource.length;
    bool acceptRanges = resource.acceptRanges;
    std::shared_ptr<void> lease = std::move(resource.lease);
    std::unique_ptr<QIODevice> body = std::move(resource.body);
    const webview::ResourceMapping* mapping = nullptr;
    if (published) {
        mimeType = resource.mimeType;
        if (auto* file = qobject_cast<QFile*>(body.get()))
            path = file->fileName();
        body.reset();
    }
    else {
        mapping = webview::findResourceMapping(state->resourceMappings, url);
        QString errorText;
        const auto accept = [task.request valueForHTTPHeaderField:@"Accept"];
        const bool mainDocumentRequest = accept && [accept containsString:@"text/html"];
        path = mapping ? webview::resolveMappedResource(*mapping, url, &errorText, mainDocumentRequest) : QString();
        if (path.isEmpty()) {
            [task didFailWithError:[NSError
                                       errorWithDomain:NSURLErrorDomain
                                                  code:NSURLErrorFileDoesNotExist
                                              userInfo:@{
                                                  NSLocalizedDescriptionKey : [NSString stringWithUTF8String:errorText.toUtf8().constData()]
                                              }]];
            return;
        }
        mimeType = QMimeDatabase().mimeTypeForFile(path).name();
        totalSize = QFileInfo(path).size();
        offset = 0;
        length = totalSize;
        acceptRanges = true;
        const auto range = [task.request valueForHTTPHeaderField:@"Range"];
        if (range) {
            static const QRegularExpression pattern(QStringLiteral("^bytes=(\\d*)-(\\d*)$"));
            const auto match = pattern.match(QString::fromUtf8(range.UTF8String));
            bool firstOk = false, lastOk = false;
            const auto first = match.captured(1);
            const auto last = match.captured(2);
            if (!match.hasMatch() || (first.isEmpty() && last.isEmpty()))
                status = webview::httpStatus::kRangeNotSatisfiable;
            else if (first.isEmpty()) {
                const auto suffix = last.toLongLong(&lastOk);
                if (!lastOk || suffix <= 0)
                    status = webview::httpStatus::kRangeNotSatisfiable;
                else {
                    offset = qMax<qint64>(0, totalSize - suffix);
                    length = totalSize - offset;
                }
            }
            else {
                offset = first.toLongLong(&firstOk);
                const auto end = last.isEmpty() ? totalSize - 1 : last.toLongLong(&lastOk);
                if (!firstOk || (!last.isEmpty() && !lastOk) || offset > end || end >= totalSize)
                    status = webview::httpStatus::kRangeNotSatisfiable;
                else
                    length = end - offset + 1;
            }
            if (status != webview::httpStatus::kRangeNotSatisfiable)
                status = webview::httpStatus::kPartialContent;
        }
    }

    if (!published && status != webview::httpStatus::kRangeNotSatisfiable) {
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly) || !file->seek(offset)) {
            [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNoPermissionsToReadFile userInfo:nil]];
            return;
        }
        body = std::move(file);
    }
    NSMutableDictionary* headers = [NSMutableDictionary dictionaryWithDictionary:@{
        @"Content-Type" : [NSString stringWithUTF8String:mimeType.toUtf8().constData()],
        @"Content-Length" :
            [NSString stringWithFormat:@"%lld", static_cast<long long>(status == webview::httpStatus::kRangeNotSatisfiable ? 0 : length)],
        @"Cache-Control" : @"no-store",
        @"Content-Disposition" : @"inline",
        @"Accept-Ranges" : acceptRanges ? @"bytes" : @"none"
    }];
    if (mapping && mimeType == QStringLiteral("text/html")) {
        const auto policy = webview::localBundleContentSecurityPolicy(mapping->externalNetworkAccess);
        if (!policy.isEmpty()) {
            headers[@"Content-Security-Policy"] = [NSString stringWithUTF8String:policy.toUtf8().constData()];
        }
    }
    if (status == webview::httpStatus::kPartialContent) {
        headers[@"Content-Range"] = [NSString stringWithFormat:@"bytes %lld-%lld/%lld", static_cast<long long>(offset),
            static_cast<long long>(offset + length - 1), static_cast<long long>(totalSize)];
    }
    else if (status == webview::httpStatus::kRangeNotSatisfiable) {
        headers[@"Content-Range"] = [NSString stringWithFormat:@"bytes */%lld", static_cast<long long>(totalSize)];
    }
    auto* response = [[NSHTTPURLResponse alloc] initWithURL:task.request.URL
                                                 statusCode:status
                                                HTTPVersion:@"HTTP/1.1"
                                               headerFields:headers];
    [task didReceiveResponse:response];
    if (status == webview::httpStatus::kOk || status == webview::httpStatus::kPartialContent) {
        auto* device = qobject_cast<QFile*>(body.get());
        QByteArray buffer(256 * 1024, Qt::Uninitialized);
        qint64 remaining = length;
        while (device && remaining > 0) {
            BOOL cancelled = NO;
            @synchronized(self) {
                cancelled = [cancelledTasks containsObject:[NSValue valueWithNonretainedObject:task]];
                if (cancelled)
                    [cancelledTasks removeObject:[NSValue valueWithNonretainedObject:task]];
            }
            if (cancelled)
                return;
            const auto count = device->read(buffer.data(), qMin<qint64>(buffer.size(), remaining));
            if (count <= 0) {
                [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNetworkConnectionLost userInfo:nil]];
                return;
            }
            [task didReceiveData:[NSData dataWithBytes:buffer.constData() length:static_cast<NSUInteger>(count)]];
            remaining -= count;
        }
    }
    @synchronized(self) {
        [cancelledTasks removeObject:[NSValue valueWithNonretainedObject:task]];
    }
    [task didFinish];
    Q_UNUSED(lease);
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task
{
    @synchronized(self) {
        [cancelledTasks addObject:[NSValue valueWithNonretainedObject:task]];
    }
}

- (instancetype)init
{
    self = [super init];
    if (self)
        cancelledTasks = [NSMutableSet set];
    return self;
}
@end

namespace webview {
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
    impl_->dataStore = impl_->options.mode == SessionMode::Ephemeral ? [WKWebsiteDataStore nonPersistentDataStore]
                                                                     : [WKWebsiteDataStore defaultDataStore];
    impl_->processPool = [[WKProcessPool alloc] init];
    auto* schemeHandler = [[SystemWebViewSchemeHandler alloc] init];
    schemeHandler->state = impl_->state;
    impl_->schemeHandler = schemeHandler;
    impl_->state->initialization.markReady();
    qCDebug(systemWebViewLifecycle).noquote() << "event=session.created" << "session=" << impl_->state->diagnosticId;
    qCInfo(systemWebViewLifecycle).noquote() << "event=session.ready" << "session=" << impl_->state->diagnosticId;
}

InitializationState WkWebViewSession::initializationState() const { return impl_->state->initialization.state(); }

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
    if (!application || impl_->applicationIds.contains(application->id()))
        return { };
    if (const auto* bundle = std::get_if<LocalBundle>(&application->source())) {
        ResourceMapping mapping { application->origin(), bundle->directory, bundle->entryDocument, bundle->spaFallback,
            bundle->externalNetworkAccess };
        QVector<ResourceMapping> candidate = impl_->state->resourceMappings;
        candidate.push_back(std::move(mapping));
        if (!validateResourceMappings(&candidate, &error)
            || resolveMappedResource(candidate.back(), application->urlForRoute({ }), &error).isEmpty())
            return { };
        impl_->state->resourceMappings = std::move(candidate);
    }
    impl_->applicationIds.insert(application->id());
    impl_->applications.push_back(application);
    return application;
}

WkWebViewSession::~WkWebViewSession()
{
    impl_->state->valid = false;
    impl_->state->callbacks = { };
    impl_->state->initialization.close();
    std::vector<WkWebView*> views;
    {
        std::lock_guard<std::mutex> lock(impl_->state->viewsMutex);
        views.assign(impl_->state->views.begin(), impl_->state->views.end());
    }
    for (auto* view : views) {
        view->close();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->state->viewsMutex);
        impl_->state->views.clear();
    }
    impl_->policy.reset();
    qCDebug(systemWebViewLifecycle).noquote() << "event=session.closed" << "session=" << impl_->state->diagnosticId;
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
    void clearData(WkSessionState& state, WKWebsiteDataStore* store, NSSet<NSString*>* types, IWebViewSession::ClearCompletion completion)
    {
        state.initialization.runWhenReady([store, types, completion = std::move(completion)](const InitializationResult& result) {
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
    clearData(*impl_->state, impl_->dataStore, [NSSet setWithObjects:WKWebsiteDataTypeDiskCache, WKWebsiteDataTypeMemoryCache, nil],
        std::move(completion));
}

void WkWebViewSession::clearCookies(ClearCompletion completion)
{
    clearData(*impl_->state, impl_->dataStore, [NSSet setWithObject:WKWebsiteDataTypeCookies], std::move(completion));
}

void WkWebViewSession::clearWebsiteData(ClearCompletion completion)
{
    clearData(*impl_->state, impl_->dataStore, [WKWebsiteDataStore allWebsiteDataTypes], std::move(completion));
}

void WkWebViewSession::setHostCallbacks(WebViewSessionHostCallbacks callbacks)
{
    if (impl_->state->valid) {
        impl_->state->callbacks = std::move(callbacks);
    }
}

bool WkWebViewSession::supports(WebViewCapability capability) const
{
    switch (capability) {
    case WebViewCapability::PersistentProfile:
        return true;
    case WebViewCapability::PrivateProfile:
        return true;
    case WebViewCapability::FileSelection:
        return true;
    case WebViewCapability::DownloadDefault:
        return false;
    case WebViewCapability::DownloadTarget:
        if (@available(macOS 11.3, *)) {
            return true;
        }
        return false;
    case WebViewCapability::Camera:
    case WebViewCapability::Microphone:
        if (@available(macOS 12.0, *)) {
            return true;
        }
        return false;
    case WebViewCapability::Location:
    case WebViewCapability::Notifications:
    case WebViewCapability::Clipboard:
        return false;
    }
    return false;
}
} // namespace webview
