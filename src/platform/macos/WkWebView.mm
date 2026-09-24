#include "platform/macos/WkWebView.h"
#include "platform/macos/WkPolicyMapping.h"
#include "platform/macos/WkSessionState.h"

#include "internal/Application.h"
#include "internal/BridgePageScript.h"
#include "internal/ResourceMapping.h"
#include "webview/HostCompletion.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewState.h"

#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QResizeEvent>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QWidget>

#include <memory>
#include <unordered_map>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

namespace {
constexpr auto kBridgeName = "systemWebView";

NSString* toNSString(const QString& value) { return [NSString stringWithUTF8String:value.toUtf8().constData()]; }

id foundationObject(const QJsonObject& object)
{
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    NSData* data = [NSData dataWithBytes:json.constData() length:json.size()];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

webview::PermissionKind permissionKind(WKMediaCaptureType type)
{
    return type == WKMediaCaptureTypeMicrophone ? webview::PermissionKind::Microphone : webview::PermissionKind::Camera;
}

class NativeViewHost final : public QWidget
{
public:
    using ResizeHandler = std::function<void()>;

    explicit NativeViewHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
    }

    ResizeHandler syncNativeView;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        scheduleNativeViewSync();
    }

    bool event(QEvent* event) override
    {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest || event->type() == QEvent::PolishRequest) {
            scheduleNativeViewSync();
        }
        return handled;
    }

private:
    void scheduleNativeViewSync()
    {
        if (syncNativeView) {
            syncNativeView();
        }
    }
};
}

namespace webview {
struct WkNavigationState {
    std::unordered_map<void*, quint64> navigationIds;

    quint64 idForNavigation(void* navigation, quint64 fallback) const
    {
        const auto found = navigationIds.find(navigation);
        return found == navigationIds.end() ? fallback : found->second;
    }
};

void installDocumentTransport(WebViewState& state, WKUserContentController* content)
{
    state.documentToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.setResourceDocumentToken(state.documentToken);
    const auto escapedToken = QString::fromUtf8(QJsonDocument(QJsonArray { state.documentToken }).toJson(QJsonDocument::Compact));
    const auto tokenLiteral = escapedToken.mid(1, escapedToken.size() - 2);
    const auto source = bridgePageScript(tokenLiteral, QStringLiteral("window.webkit.messageHandlers.systemWebView.postMessage"), { });
    [content removeAllUserScripts];
    auto* script = [[WKUserScript alloc] initWithSource:toNSString(source)
                                          injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                       forMainFrameOnly:YES];
    [content addUserScript:script];
    state.documentTransportPrepared = true;
}

class WkBridgeTransport final : public BridgeTransport
{
public:
    explicit WkBridgeTransport(WKWebView* view)
        : view_(view)
    {
    }
    bool send(const QByteArray& bytes) override
    {
        if (!view_)
            return false;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return false;
        if (@available(macOS 11.0, *)) {
            [view_ callAsyncJavaScript:@"window.__systemWebViewReceive(message);"
                             arguments:@ { @"message" : foundationObject(document.object()) }
                               inFrame:nil
                        inContentWorld:[WKContentWorld pageWorld]
                     completionHandler:^(id, NSError*) { }];
            return true;
        }
        const auto json = QString::fromUtf8(bytes);
        const auto script = QStringLiteral("window.__systemWebViewReceive(%1);").arg(json);
        [view_ evaluateJavaScript:toNSString(script) completionHandler:^(id, NSError*) { }];
        return true;
    }
    void invalidate() override { view_ = nil; }

private:
    WKWebView* view_;
};

class WkWebView::Impl
{
public:
    NativeViewHost* container = nullptr;
    WKWebView* view = nil;
    std::shared_ptr<WebViewState> state;
    std::shared_ptr<WkNavigationState> navigationState = std::make_shared<WkNavigationState>();
    std::shared_ptr<WkSessionState> sessionState;
    id messageDelegate = nil;
    id uiDelegate = nil;
    id navigationDelegate = nil;
    id downloadDelegate = nil;
    bool nativeViewAttachmentRequested = false;
};
} // namespace webview

@interface SystemWebViewMessageDelegate : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) webview::WebViewState* state;
@end

@interface SystemWebViewUIDelegate : NSObject <WKUIDelegate>
@property (nonatomic, assign) webview::WebViewState* state;
@property (nonatomic, assign) webview::WkWebView* owner;
@end

@class SystemWebViewDownloadDelegate;

@interface SystemWebViewNavigationDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) webview::WebViewState* state;
@property (nonatomic, assign) webview::WkNavigationState* navigationState;
@property (nonatomic, assign) SystemWebViewDownloadDelegate* downloadDelegate;
@end

@interface SystemWebViewDownloadDelegate : NSObject <WKDownloadDelegate> {
@public
    std::weak_ptr<webview::WebViewState> state;
    QPointer<QWidget> context;
}
@end

@implementation SystemWebViewMessageDelegate
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message
{
    const QUrl frameUrl(QString::fromUtf8(message.frameInfo.request.URL.absoluteString.UTF8String));
    if (![message.name isEqualToString:@(kBridgeName)] || !self.state || self.state->lifetime.isClosed() || !message.frameInfo.mainFrame
        || self.state->bridgeOrigin != webview::normalizedOrigin(self.state->committedUrl)
        || !self.state->policy->allowsBridge(self.state->committedUrl)
        || webview::normalizedOrigin(frameUrl) != webview::normalizedOrigin(self.state->committedUrl)) {
        return;
    }
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (!data || error) {
        return;
    }
    QJsonObject wrapper;
    if (webview::parseMessage(QString::fromUtf8(static_cast<const char*>(data.bytes), data.length), &wrapper)
        && wrapper.value(QStringLiteral("documentToken")).toString() == self.state->documentToken
        && wrapper.value(QStringLiteral("message")).isObject()) {
        const auto object = wrapper.value(QStringLiteral("message")).toObject();
        if (!object.value(QStringLiteral("type")).isString() || !object.value(QStringLiteral("payload")).isObject()) {
            return;
        }
        self.state->bridge->receive(QJsonDocument(object).toJson(QJsonDocument::Compact), frameUrl);
    }
}
@end

@implementation SystemWebViewUIDelegate
- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)navigationAction
                    windowFeatures:(WKWindowFeatures*)windowFeatures
{
    if (!self.state || self.state->lifetime.isClosed() || !self.owner || !self.state->callbacks.onNewWindow) {
        return nil;
    }
    const QUrl url(QString::fromUtf8(navigationAction.request.URL.absoluteString.UTF8String));
    const webview::NewWindowRequest request { url, navigationAction.navigationType == WKNavigationTypeLinkActivated };
    if (self.state->policy->decideNewWindow(request) != webview::NewWindowDecision::Allow) {
        return nil;
    }
    return static_cast<WKWebView*>(self.owner->createPopup(configuration, request));
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                          initiatedByFrame:(WKFrameInfo*)frame
                                      type:(WKMediaCaptureType)type
                           decisionHandler:(void (^)(WKPermissionDecision decision))decisionHandler
{
    if (!self.state || self.state->lifetime.isClosed() || !frame.mainFrame) {
        decisionHandler(WKPermissionDecisionDeny);
        return;
    }
    const QUrl originUrl(QStringLiteral("%1://%2:%3")
            .arg(QString::fromUtf8(origin.protocol.UTF8String), QString::fromUtf8(origin.host.UTF8String))
            .arg(origin.port));
    const auto decision
        = webview::decideNativePermission(*self.state->policy, { permissionKind(type), webview::normalizedOrigin(originUrl) });
    decisionHandler(decision == webview::NativePermissionDecision::Grant ? WKPermissionDecisionGrant
            : decision == webview::NativePermissionDecision::Prompt      ? WKPermissionDecisionPrompt
                                                                         : WKPermissionDecisionDeny);
}

- (void)webView:(WKWebView*)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
              initiatedByFrame:(WKFrameInfo*)frame
             completionHandler:(void (^)(NSArray<NSURL*>* URLs))completionHandler
{
    if (!self.state || self.state->lifetime.isClosed() || !frame.mainFrame
        || webview::decideNativePermission(*self.state->policy,
               { webview::PermissionKind::FilePicker,
                   webview::normalizedOrigin(QUrl(QString::fromUtf8(frame.request.URL.absoluteString.UTF8String))) })
            != webview::NativePermissionDecision::Grant
        || !self.state->callbacks.onSelectFiles) {
        completionHandler(nil);
        return;
    }
    const QUrl documentUrl(QString::fromUtf8(frame.request.URL.absoluteString.UTF8String));
    const webview::FileSelectionRequest request { webview::normalizedOrigin(documentUrl), documentUrl, parameters.allowsMultipleSelection,
        parameters.allowsDirectories };
    auto guard = std::make_shared<webview::HostCompletionGuard>(self.owner->stateForHostCompletion());
    const QPointer<QWidget> context(self.owner->widget());
    const auto completion = [completionHandler, guard, context, request](webview::FileSelectionResult result) {
        const auto access = guard->claim();
        if (access.claim != webview::HostCompletionClaim::Accepted || !context) {
            return;
        }
        result = webview::normalizeFileSelectionResult(request, std::move(result));
        QMetaObject::invokeMethod(
            context.data(),
            [completionHandler, state = access.state, result = std::move(result)] {
                if (state->lifetime.isClosed()) {
                    return;
                }
                if (result.status != webview::FileSelectionStatus::Selected) {
                    completionHandler(nil);
                    return;
                }
                NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:result.paths.size()];
                for (const auto& path : result.paths) {
                    [urls addObject:[NSURL fileURLWithPath:toNSString(path)]];
                }
                completionHandler(urls);
            },
            Qt::QueuedConnection);
    };
    self.state->callbacks.onSelectFiles(request, completion);
}
@end

@implementation SystemWebViewDownloadDelegate
- (void)download:(WKDownload*)download
    decideDestinationUsingResponse:(NSURLResponse*)response
                 suggestedFilename:(NSString*)suggestedFilename
                 completionHandler:(void (^)(NSURL* destinationURL))completionHandler
{
    const auto currentState = state.lock();
    if (!currentState || currentState->lifetime.isClosed()
        || currentState->policy->decideDownload(
               { QUrl(QString::fromUtf8(response.URL.absoluteString.UTF8String)), currentState->committedUrl,
                   QUrl(QString::fromUtf8(response.URL.absoluteString.UTF8String)), QString::fromUtf8(suggestedFilename.UTF8String) })
            != webview::DownloadDecision::Allow
        || !currentState->callbacks.onResolveDownload || !context) {
        completionHandler(nil);
        return;
    }
    const webview::DownloadRequest request { QUrl(QString::fromUtf8(response.URL.absoluteString.UTF8String)),
        webview::normalizedOrigin(currentState->committedUrl), QUrl(QString::fromUtf8(response.URL.absoluteString.UTF8String)),
        QString::fromUtf8(suggestedFilename.UTF8String) };
    auto guard = std::make_shared<webview::HostCompletionGuard>(currentState);
    const QPointer<QWidget> dispatchContext = context;
    const auto completion = [completionHandler, guard, dispatchContext](webview::DownloadResolution resolution) {
        const auto access = guard->claim();
        if (access.claim != webview::HostCompletionClaim::Accepted || !dispatchContext) {
            return;
        }
        QMetaObject::invokeMethod(
            dispatchContext.data(),
            [completionHandler, statePtr = access.state, resolution = std::move(resolution)] {
                if (statePtr->lifetime.isClosed() || resolution.status != webview::DownloadResolutionStatus::Resolved
                    || resolution.target.handling != webview::DownloadHandling::TargetPath || resolution.target.filePath.isEmpty()) {
                    completionHandler(nil);
                    return;
                }
                completionHandler([NSURL fileURLWithPath:toNSString(resolution.target.filePath)]);
            },
            Qt::QueuedConnection);
    };
    currentState->callbacks.onResolveDownload(request, completion);
}
@end

@implementation SystemWebViewNavigationDelegate
- (void)webView:(WKWebView*)webView navigationAction:(WKNavigationAction*)navigationAction didBecomeDownload:(WKDownload*)download
{
    download.delegate = self.downloadDelegate;
}

- (void)webView:(WKWebView*)webView navigationResponse:(WKNavigationResponse*)navigationResponse didBecomeDownload:(WKDownload*)download
{
    download.delegate = self.downloadDelegate;
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)navigationAction
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    if (!self.state || self.state->lifetime.isClosed()) {
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    const QUrl url(QString::fromUtf8(navigationAction.request.URL.absoluteString.UTF8String));
    const bool isMainFrame = navigationAction.targetFrame == nil || navigationAction.targetFrame.mainFrame;
    const bool isUserInitiated = navigationAction.navigationType == WKNavigationTypeLinkActivated
        || navigationAction.navigationType == WKNavigationTypeFormSubmitted;
    const bool isRedirect
        = isMainFrame && self.state->provisionalMainFrameNavigation && !isUserInitiated && !self.state->explicitMainFrameNavigationPending;
    const webview::NavigationRequest request { url, isMainFrame, isUserInitiated, isRedirect };
    if (@available(macOS 11.3, *)) {
        if (navigationAction.shouldPerformDownload) {
            if (isMainFrame) {
                self.state->explicitMainFrameNavigationPending = false;
                self.state->provisionalMainFrameNavigation = false;
            }
            decisionHandler(
                self.state->policy->decideDownload({ url, webview::normalizedOrigin(self.state->committedUrl), url, url.fileName() })
                            == webview::DownloadDecision::Allow
                        && self.state->callbacks.onResolveDownload
                    ? WKNavigationActionPolicyDownload
                    : WKNavigationActionPolicyCancel);
            return;
        }
    }
    const auto decision = self.state->policy->decideNavigation(request);
    if (decision == webview::NavigationDecision::Allow) {
        if (isMainFrame) {
            self.state->explicitMainFrameNavigationPending = false;
            if (!isRedirect) {
                self.state->provisionalMainFrameNavigation = false;
                self.state->invalidateDocument();
                self.state->committedUrl = QUrl();
                if (self.state->documentTransportPrepared) {
                    self.state->documentTransportPrepared = false;
                }
                else {
                    webview::installDocumentTransport(*self.state, webView.configuration.userContentController);
                    self.state->documentTransportPrepared = false;
                }
            }
        }
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    if (decision == webview::NavigationDecision::OpenExternally && self.state->callbacks.onOpenExternal) {
        self.state->callbacks.onOpenExternal(url);
    }
    if (isMainFrame) {
        self.state->explicitMainFrameNavigationPending = false;
        self.state->provisionalMainFrameNavigation = false;
    }
    decisionHandler(WKNavigationActionPolicyCancel);
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)navigationResponse
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler
{
    if (!self.state || self.state->lifetime.isClosed()) {
        decisionHandler(WKNavigationResponsePolicyCancel);
        return;
    }
    if (navigationResponse.canShowMIMEType) {
        decisionHandler(WKNavigationResponsePolicyAllow);
        return;
    }
    const QUrl url(QString::fromUtf8(navigationResponse.response.URL.absoluteString.UTF8String));
    if (@available(macOS 11.3, *)) {
        decisionHandler(
            self.state->policy->decideDownload({ url, webview::normalizedOrigin(self.state->committedUrl), url, url.fileName() })
                        == webview::DownloadDecision::Allow
                    && self.state->callbacks.onResolveDownload
                ? WKNavigationResponsePolicyDownload
                : WKNavigationResponsePolicyCancel);
    }
    else {
        decisionHandler(WKNavigationResponsePolicyCancel);
    }
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation
{
    if (!self.state || self.state->lifetime.isClosed()) {
        return;
    }
    ++self.state->navigationId;
    self.state->provisionalMainFrameNavigation = true;
    self.navigationState->navigationIds[static_cast<void*>(navigation)] = self.state->navigationId;
    self.state->emitLoad(
        webview::LoadState::Started, self.state->navigationId, QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)));
}

- (void)webView:(WKWebView*)webView didReceiveServerRedirectForProvisionalNavigation:(WKNavigation*)navigation
{
    if (self.state) {
        self.state->emitLoad(webview::LoadState::Redirected,
            self.navigationState->idForNavigation(static_cast<void*>(navigation), self.state->navigationId),
            QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)));
    }
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation
{
    if (!self.state || self.state->lifetime.isClosed()) {
        return;
    }
    self.state->committedUrl = QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String));
    if (!self.state->resourceOrigin.isEmpty()) {
        self.state->setResourceContext(self.state->resourceOrigin, self.state->committedUrl, self.state->documentToken);
    }
    self.state->emitLoad(webview::LoadState::Committed,
        self.navigationState->idForNavigation(static_cast<void*>(navigation), self.state->navigationId), self.state->committedUrl);
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation
{
    if (self.state) {
        const auto navigationId = self.navigationState->idForNavigation(static_cast<void*>(navigation), self.state->navigationId);
        self.state->emitLoad(webview::LoadState::Finished, navigationId, QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)));
        self.navigationState->navigationIds.erase(static_cast<void*>(navigation));
        self.state->provisionalMainFrameNavigation = false;
    }
}

- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error
{
    if (self.state) {
        const auto navigationId = self.navigationState->idForNavigation(static_cast<void*>(navigation), self.state->navigationId);
        self.state->emitLoad(webview::LoadState::Failed, navigationId,
            QUrl(QString::fromUtf8(error.userInfo[NSURLErrorFailingURLErrorKey]
                    ? [error.userInfo[NSURLErrorFailingURLErrorKey] absoluteString].UTF8String
                    : "")),
            QString::fromUtf8(error.localizedDescription.UTF8String));
        self.navigationState->navigationIds.erase(static_cast<void*>(navigation));
        self.state->provisionalMainFrameNavigation = false;
    }
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error
{
    if (self.state) {
        const auto navigationId = self.navigationState->idForNavigation(static_cast<void*>(navigation), self.state->navigationId);
        self.state->emitLoad(webview::LoadState::Failed, navigationId, QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)),
            QString::fromUtf8(error.localizedDescription.UTF8String));
        self.navigationState->navigationIds.erase(static_cast<void*>(navigation));
        self.state->provisionalMainFrameNavigation = false;
    }
}
@end

namespace webview {
WkWebView::WkWebView(QWidget* parent, void* configuration, WebViewPolicyPtr policy, std::shared_ptr<WkSessionState> sessionState)
    : impl_(std::make_unique<Impl>())
{
    initialize(configuration, std::move(policy), std::move(sessionState));
    impl_->container->setParent(parent);
}

void WkWebView::initialize(void* configuration, WebViewPolicyPtr policy, std::shared_ptr<WkSessionState> sessionState)
{
    impl_->state = std::make_shared<WebViewState>();
    impl_->state->policy = policy ? std::move(policy) : createDefaultWebViewPolicy();
    impl_->state->bindBridgePolicy();
    impl_->sessionState = std::move(sessionState);
    impl_->container = new NativeViewHost(nullptr);
    auto* content = [[WKUserContentController alloc] init];
    impl_->messageDelegate = [[SystemWebViewMessageDelegate alloc] init];
    static_cast<SystemWebViewMessageDelegate*>(impl_->messageDelegate).state = impl_->state.get();
    [content addScriptMessageHandler:impl_->messageDelegate name:@(kBridgeName)];
    installDocumentTransport(*impl_->state, content);

    auto* nativeConfiguration = static_cast<WKWebViewConfiguration*>(configuration);
    if (!nativeConfiguration) {
        nativeConfiguration = [[WKWebViewConfiguration alloc] init];
    }
    nativeConfiguration.userContentController = content;
    auto* uiDelegate = [[SystemWebViewUIDelegate alloc] init];
    uiDelegate.state = impl_->state.get();
    uiDelegate.owner = this;
    impl_->uiDelegate = uiDelegate;
    auto* navigationDelegate = [[SystemWebViewNavigationDelegate alloc] init];
    navigationDelegate.state = impl_->state.get();
    navigationDelegate.navigationState = impl_->navigationState.get();
    impl_->navigationDelegate = navigationDelegate;
    auto* downloadDelegate = [[SystemWebViewDownloadDelegate alloc] init];
    downloadDelegate->state = impl_->state;
    downloadDelegate->context = impl_->container;
    impl_->downloadDelegate = downloadDelegate;
    impl_->view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1) configuration:nativeConfiguration];
    impl_->state->bridge->setTransport(std::make_unique<WkBridgeTransport>(impl_->view));
    impl_->view.UIDelegate = uiDelegate;
    impl_->view.navigationDelegate = navigationDelegate;
    if (@available(macOS 11.3, *)) {
        navigationDelegate.downloadDelegate = downloadDelegate;
    }
    impl_->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    impl_->container->syncNativeView
        = [container = impl_->container, view = impl_->view, attachmentRequested = &impl_->nativeViewAttachmentRequested] {
              if (!*attachmentRequested || !container->parentWidget() || !container->isVisible() || container->size().isEmpty()) {
                  return;
              }
              auto* hostView = reinterpret_cast<NSView*>(container->winId());
              [hostView layoutSubtreeIfNeeded];
              if (NSIsEmptyRect(hostView.bounds)) {
                  return;
              }
              view.frame = hostView.bounds;
              if (view.superview != hostView) {
                  [hostView addSubview:view];
              }
          };
    if (impl_->sessionState && impl_->sessionState->valid) {
        std::lock_guard<std::mutex> lock(impl_->sessionState->viewsMutex);
        impl_->sessionState->views.insert(this);
        impl_->state->markReady();
    }
    else {
        close();
    }
}

WkWebView::~WkWebView() { close(); }

QWidget* WkWebView::widget() { return impl_->container; }

InitializationState WkWebView::initializationState() const { return impl_->state->initializationState(); }

void WkWebView::whenInitialized(InitializationCompletion completion)
{
    if (completion) {
        impl_->state->whenInitialized(std::move(completion));
    }
}

void WkWebView::attachNativeView()
{
    if (impl_->state->lifetime.isClosed()) {
        return;
    }
    impl_->nativeViewAttachmentRequested = true;
    impl_->state->runWhenReady([this](const InitializationResult& result) {
        if (result.state == InitializationState::Ready && impl_->nativeViewAttachmentRequested) {
            impl_->container->syncNativeView();
        }
        else if (result.state != InitializationState::Ready) {
            impl_->nativeViewAttachmentRequested = false;
        }
    });
}

void WkWebView::detachNativeView()
{
    if (impl_->state->lifetime.isClosed()) {
        return;
    }
    impl_->nativeViewAttachmentRequested = false;
    [impl_->view removeFromSuperview];
}

void WkWebView::open(WebApplicationPtr application, const QString& route)
{
    if (!application)
        return;
    impl_->state->bridgeOrigin = application->bridgeAccess() == BridgeAccess::Allowed ? normalizedOrigin(application->origin()) : QUrl();
    impl_->state->resourceOrigin = QUrl(QStringLiteral("app://%1").arg(application->id()));
    impl_->state->setResourceContext(impl_->state->resourceOrigin, application->origin(), impl_->state->documentToken);
    navigate(application->urlForRoute(route));
}

void WkWebView::navigate(const QUrl& url)
{
    impl_->state->runWhenReady([this, url](const InitializationResult& result) {
        if (result.state != InitializationState::Ready) {
            if (impl_->state->callbacks.onLoad) {
                impl_->state->callbacks.onLoad({ LoadState::Failed, url, result.error, ++impl_->state->navigationId, true });
            }
            return;
        }
        impl_->state->provisionalMainFrameNavigation = false;
        impl_->state->explicitMainFrameNavigationPending = true;
        [impl_->view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:toNSString(url.toString())]]];
    });
}

void WkWebView::loadDocument(const QString& html, const QUrl& baseUrl)
{
    impl_->state->runWhenReady([this, html, baseUrl](const InitializationResult& result) {
        if (result.state != InitializationState::Ready) {
            if (impl_->state->callbacks.onLoad) {
                impl_->state->callbacks.onLoad({ LoadState::Failed, baseUrl, result.error, ++impl_->state->navigationId, true });
            }
            return;
        }
        const NavigationRequest request { baseUrl, true, false, false };
        if (impl_->state->policy->decideNavigation(request) != NavigationDecision::Allow) {
            impl_->state->emitLoad(
                LoadState::Failed, ++impl_->state->navigationId, baseUrl, QStringLiteral("The HTML base URL was rejected by policy."));
            return;
        }
        if (baseUrl.scheme().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0
            && (!impl_->sessionState || !findResourceMapping(impl_->sessionState->resourceMappings, baseUrl))) {
            impl_->state->emitLoad(LoadState::Failed, ++impl_->state->navigationId, baseUrl,
                QStringLiteral("The app origin has no configured resource mapping."));
            return;
        }
        impl_->state->invalidateDocument();
        impl_->state->committedUrl = QUrl();
        impl_->state->provisionalMainFrameNavigation = false;
        impl_->state->explicitMainFrameNavigationPending = true;
        installDocumentTransport(*impl_->state, impl_->view.configuration.userContentController);
        [impl_->view loadHTMLString:toNSString(html) baseURL:[NSURL URLWithString:toNSString(baseUrl.toString())]];
    });
}

void WkWebView::stop()
{
    if (!impl_->state->lifetime.isClosed()) {
        [impl_->view stopLoading];
    }
}

void WkWebView::reload()
{
    if (!impl_->state->lifetime.isClosed()) {
        impl_->state->invalidateDocument();
        impl_->state->committedUrl = QUrl();
        impl_->state->provisionalMainFrameNavigation = false;
        impl_->state->explicitMainFrameNavigationPending = true;
        installDocumentTransport(*impl_->state, impl_->view.configuration.userContentController);
        [impl_->view reload];
    }
}

void WkWebView::close()
{
    if (impl_->state->lifetime.isClosed()) {
        return;
    }
    impl_->state->close();
    impl_->state->callbacks = { };
    impl_->state->documentToken.clear();
    impl_->state->documentTransportPrepared = false;
    impl_->state->provisionalMainFrameNavigation = false;
    impl_->state->explicitMainFrameNavigationPending = false;
    impl_->navigationState->navigationIds.clear();
    impl_->state->policy.reset();
    if (impl_->sessionState) {
        std::lock_guard<std::mutex> lock(impl_->sessionState->viewsMutex);
        impl_->sessionState->views.erase(this);
    }
    [impl_->view stopLoading];
    static_cast<SystemWebViewMessageDelegate*>(impl_->messageDelegate).state = nullptr;
    static_cast<SystemWebViewUIDelegate*>(impl_->uiDelegate).state = nullptr;
    static_cast<SystemWebViewUIDelegate*>(impl_->uiDelegate).owner = nullptr;
    static_cast<SystemWebViewNavigationDelegate*>(impl_->navigationDelegate).state = nullptr;
    impl_->view.navigationDelegate = nil;
    impl_->view.UIDelegate = nil;
    [impl_->view.configuration.userContentController removeScriptMessageHandlerForName:@(kBridgeName)];
    [impl_->view removeFromSuperview];
    impl_->container->syncNativeView = { };
    impl_->nativeViewAttachmentRequested = false;
    impl_->messageDelegate = nil;
    impl_->uiDelegate = nil;
    impl_->navigationDelegate = nil;
    impl_->downloadDelegate = nil;
    impl_->view = nil;
}

bool WkWebView::isClosed() const { return impl_->state->lifetime.isClosed(); }

WebViewBridge& WkWebView::bridge() { return *impl_->state->bridge; }
WebResourceManager& WkWebView::resources() { return *impl_->state->resources; }

void WkWebView::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    if (!impl_->state->lifetime.isClosed()) {
        impl_->state->callbacks = std::move(callbacks);
    }
}

void* WkWebView::createPopup(void* configuration, const NewWindowRequest& request)
{
    if (impl_->state->lifetime.isClosed() || !impl_->state->callbacks.onNewWindow || !impl_->sessionState || !impl_->sessionState->valid) {
        return nullptr;
    }
    WebViewPtr child = std::unique_ptr<WkWebView>(new WkWebView(nullptr, configuration, impl_->state->policy, impl_->sessionState));
    auto* concreteChild = static_cast<WkWebView*>(child.get());
    auto* nativeView = concreteChild->impl_->view;
    const std::weak_ptr<WebViewState> childState = concreteChild->impl_->state;
    impl_->state->callbacks.onNewWindow(request, std::move(child));
    const auto acceptedState = childState.lock();
    if (!acceptedState || acceptedState->lifetime.isClosed()) {
        return nullptr;
    }
    return nativeView;
}

std::shared_ptr<WebViewState> WkWebView::stateForHostCompletion() const { return impl_->state; }

bool WkWebView::ownsNativeView(void* nativeView) const { return static_cast<void*>(impl_->view) == nativeView; }

} // namespace webview
