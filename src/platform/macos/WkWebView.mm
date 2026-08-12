#include "platform/macos/WkWebView.h"

#include "webview/JsonMessage.h"
#include "webview/DocumentLifetime.h"

#include <QEvent>
#include <QJsonDocument>
#include <QResizeEvent>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <unordered_map>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

namespace {
constexpr auto kBridgeName = "systemWebView";

NSString* toNSString(const QString& value) { return [NSString stringWithUTF8String:value.toUtf8().constData()]; }

QString originForUrl(const QUrl& url)
{
    QUrl origin;
    origin.setScheme(url.scheme().toLower());
    origin.setHost(url.host().toLower());
    if (url.port() >= 0) {
        origin.setPort(url.port());
    }
    return origin.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::StripTrailingSlash);
}

id foundationObject(const QJsonObject& object)
{
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    NSData* data = [NSData dataWithBytes:json.constData() length:json.size()];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

class NativeViewHost final : public QWidget
{
public:
    using ResizeHandler = std::function<void()>;

    explicit NativeViewHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
        winId();
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
            QTimer::singleShot(0, this, [this] { syncNativeView(); });
        }
    }
};
}

namespace webview {
struct NativeState {
    WebViewHostCallbacks callbacks;
    WebViewPolicyPtr policy;
    std::function<void*(void*, const NewWindowRequest&)> createWebView;
    QUrl committedUrl;
    quint64 navigationId = 0;
    DocumentLifetime lifetime;
    std::unordered_map<void*, quint64> navigationIds;

    void emitLoad(LoadState loadState, quint64 eventNavigationId, const QUrl& url = { }, const QString& error = { })
    {
        if (!lifetime.isClosed() && callbacks.load) {
            callbacks.load({ loadState, url, error, eventNavigationId, true });
        }
    }

    quint64 idForNavigation(void* navigation) const
    {
        const auto found = navigationIds.find(navigation);
        return found == navigationIds.end() ? navigationId : found->second;
    }
};

class WkWebView::Impl
{
public:
    NativeViewHost* container = nullptr;
    WKWebView* view = nil;
    std::shared_ptr<NativeState> state;
    id messageDelegate = nil;
    id uiDelegate = nil;
    id navigationDelegate = nil;
};
} // namespace webview

@interface SystemWebViewMessageDelegate : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) webview::NativeState* state;
@end

@interface SystemWebViewUIDelegate : NSObject <WKUIDelegate>
@property (nonatomic, assign) webview::NativeState* state;
@end

@interface SystemWebViewNavigationDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) webview::NativeState* state;
@end

@implementation SystemWebViewMessageDelegate
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message
{
    const QUrl frameUrl(QString::fromUtf8(message.frameInfo.request.URL.absoluteString.UTF8String));
    if (![message.name isEqualToString:@(kBridgeName)] || !self.state || self.state->lifetime.isClosed()
        || !self.state->callbacks.message || !message.frameInfo.mainFrame
        || !self.state->policy->allowsBridge(self.state->committedUrl)
        || originForUrl(frameUrl) != originForUrl(self.state->committedUrl)) {
        return;
    }
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (!data || error) {
        return;
    }
    QJsonObject object;
    if (webview::parseMessage(QString::fromUtf8(static_cast<const char*>(data.bytes), data.length), &object)) {
        webview::BridgeMessage bridgeMessage;
        bridgeMessage.version = object.value(QStringLiteral("version")).toInt(-1);
        bridgeMessage.type = object.value(QStringLiteral("type")).toString();
        bridgeMessage.payload = object.value(QStringLiteral("payload")).toObject();
        QString validationError;
        if (self.state->policy->validateBridgeMessage(bridgeMessage, &validationError)) {
            self.state->callbacks.message(bridgeMessage);
        }
    }
}
@end

@implementation SystemWebViewUIDelegate
- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)navigationAction
                    windowFeatures:(WKWindowFeatures*)windowFeatures
{
    if (!self.state || self.state->lifetime.isClosed() || !self.state->createWebView || !self.state->callbacks.newWindow) {
        return nil;
    }
    const QUrl url(QString::fromUtf8(navigationAction.request.URL.absoluteString.UTF8String));
    const webview::NewWindowRequest request { url, navigationAction.navigationType == WKNavigationTypeLinkActivated };
    if (self.state->policy->decideNewWindow(request) != webview::NewWindowDecision::Allow) {
        return nil;
    }
    return static_cast<WKWebView*>(self.state->createWebView(configuration, request));
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                          initiatedByFrame:(WKFrameInfo*)frame
                                     type:(WKMediaCaptureType)type
                          decisionHandler:(void (^)(WKPermissionDecision decision))decisionHandler
{
    decisionHandler(WKPermissionDecisionDeny);
}
@end

@implementation SystemWebViewNavigationDelegate
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
    const webview::NavigationRequest request { url, isMainFrame, isUserInitiated, false };
    const auto decision = self.state->policy->decideNavigation(request);
    if (decision == webview::NavigationDecision::Allow) {
        if (isMainFrame) {
            self.state->lifetime.invalidate();
            self.state->committedUrl = QUrl();
        }
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    if (decision == webview::NavigationDecision::OpenExternally && self.state->callbacks.openExternal) {
        self.state->callbacks.openExternal(url);
    }
    decisionHandler(WKNavigationActionPolicyCancel);
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationResponse:(WKNavigationResponse*)navigationResponse
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler
{
    decisionHandler(navigationResponse.canShowMIMEType ? WKNavigationResponsePolicyAllow
                                                       : WKNavigationResponsePolicyCancel);
}

- (void)webView:(WKWebView*)webView didStartProvisionalNavigation:(WKNavigation*)navigation
{
    if (!self.state || self.state->lifetime.isClosed()) {
        return;
    }
    ++self.state->navigationId;
    self.state->navigationIds[static_cast<void*>(navigation)] = self.state->navigationId;
    self.state->emitLoad(webview::LoadState::Started, self.state->navigationId,
        QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)));
}

- (void)webView:(WKWebView*)webView didReceiveServerRedirectForProvisionalNavigation:(WKNavigation*)navigation
{
    if (self.state) {
        self.state->emitLoad(webview::LoadState::Redirected,
            self.state->idForNavigation(static_cast<void*>(navigation)),
            QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)));
    }
}

- (void)webView:(WKWebView*)webView didCommitNavigation:(WKNavigation*)navigation
{
    if (!self.state || self.state->lifetime.isClosed()) {
        return;
    }
    self.state->committedUrl = QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String));
    self.state->emitLoad(webview::LoadState::Committed,
        self.state->idForNavigation(static_cast<void*>(navigation)), self.state->committedUrl);
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation
{
    if (self.state) {
        const auto navigationId = self.state->idForNavigation(static_cast<void*>(navigation));
        self.state->emitLoad(webview::LoadState::Finished, navigationId,
            QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)));
        self.state->navigationIds.erase(static_cast<void*>(navigation));
    }
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error
{
    if (self.state) {
        const auto navigationId = self.state->idForNavigation(static_cast<void*>(navigation));
        self.state->emitLoad(webview::LoadState::Failed, navigationId,
            QUrl(QString::fromUtf8(error.userInfo[NSURLErrorFailingURLErrorKey]
                                       ? [error.userInfo[NSURLErrorFailingURLErrorKey] absoluteString].UTF8String
                                       : "")),
            QString::fromUtf8(error.localizedDescription.UTF8String));
        self.state->navigationIds.erase(static_cast<void*>(navigation));
    }
}

- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error
{
    if (self.state) {
        const auto navigationId = self.state->idForNavigation(static_cast<void*>(navigation));
        self.state->emitLoad(webview::LoadState::Failed, navigationId,
            QUrl(QString::fromUtf8(webView.URL.absoluteString.UTF8String)),
            QString::fromUtf8(error.localizedDescription.UTF8String));
        self.state->navigationIds.erase(static_cast<void*>(navigation));
    }
}
@end

namespace webview {
WkWebView::WkWebView(QWidget* parent, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>())
{
    initialize(nullptr, std::move(policy));
    impl_->container->setParent(parent);
}

WkWebView::WkWebView(QWidget* parent, void* configuration, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>())
{
    initialize(configuration, std::move(policy));
    impl_->container->setParent(parent);
}

void WkWebView::initialize(void* configuration, WebViewPolicyPtr policy)
{
    impl_->state = std::make_shared<NativeState>();
    impl_->state->policy = policy ? std::move(policy) : createDefaultWebViewPolicy();
    impl_->container = new NativeViewHost(nullptr);
    auto* content = [[WKUserContentController alloc] init];
    impl_->messageDelegate = [[SystemWebViewMessageDelegate alloc] init];
    static_cast<SystemWebViewMessageDelegate*>(impl_->messageDelegate).state = impl_->state.get();
    [content addScriptMessageHandler:impl_->messageDelegate name:@(kBridgeName)];

    NSString* scriptSource = @"window.__systemWebViewReceive = function(message) { window.dispatchEvent(new "
                             @"CustomEvent('system-webview-message', { detail: message })); };";
    auto* script = [[WKUserScript alloc] initWithSource:scriptSource
                                          injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                       forMainFrameOnly:YES];
    [content addUserScript:script];

    auto* nativeConfiguration = static_cast<WKWebViewConfiguration*>(configuration);
    if (!nativeConfiguration) {
        nativeConfiguration = [[WKWebViewConfiguration alloc] init];
    }
    nativeConfiguration.userContentController = content;
    auto* uiDelegate = [[SystemWebViewUIDelegate alloc] init];
    uiDelegate.state = impl_->state.get();
    impl_->uiDelegate = uiDelegate;
    auto* navigationDelegate = [[SystemWebViewNavigationDelegate alloc] init];
    navigationDelegate.state = impl_->state.get();
    impl_->navigationDelegate = navigationDelegate;
    impl_->state->createWebView = [this](void* childConfiguration, const NewWindowRequest& request) -> void* {
        if (impl_->state->lifetime.isClosed() || !impl_->state->callbacks.newWindow) {
            return nullptr;
        }
        auto child = std::unique_ptr<WkWebView>(new WkWebView(nullptr, childConfiguration, impl_->state->policy));
        auto* nativeView = child->impl_->view;
        impl_->state->callbacks.newWindow(request, std::move(child));
        return nativeView;
    };
    impl_->view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1) configuration:nativeConfiguration];
    impl_->view.UIDelegate = uiDelegate;
    impl_->view.navigationDelegate = navigationDelegate;
    impl_->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    auto* hostView = reinterpret_cast<NSView*>(impl_->container->winId());
    impl_->container->syncNativeView = [hostView, view = impl_->view] {
        [hostView layoutSubtreeIfNeeded];
        view.frame = hostView.bounds;
    };
    [hostView addSubview:impl_->view];
    impl_->container->syncNativeView();
}

WkWebView::~WkWebView() { close(); }

QWidget* WkWebView::widget() { return impl_->container; }

void WkWebView::load(const QUrl& url)
{
    if (impl_->state->lifetime.isClosed()) {
        return;
    }
    [impl_->view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:toNSString(url.toString())]]];
}

void WkWebView::setHtml(const QString& html, const QUrl& baseUrl)
{
    if (impl_->state->lifetime.isClosed()) {
        return;
    }
    const NavigationRequest request { baseUrl, true, false, false };
    if (impl_->state->policy->decideNavigation(request) != NavigationDecision::Allow) {
        return;
    }
    [impl_->view loadHTMLString:toNSString(html) baseURL:[NSURL URLWithString:toNSString(baseUrl.toString())]];
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
        [impl_->view reload];
    }
}

void WkWebView::close()
{
    if (impl_->state->lifetime.isClosed()) {
        return;
    }
    impl_->state->lifetime.close();
    impl_->state->callbacks = { };
    impl_->state->createWebView = { };
    impl_->state->navigationIds.clear();
    [impl_->view stopLoading];
    static_cast<SystemWebViewMessageDelegate*>(impl_->messageDelegate).state = nullptr;
    static_cast<SystemWebViewUIDelegate*>(impl_->uiDelegate).state = nullptr;
    static_cast<SystemWebViewNavigationDelegate*>(impl_->navigationDelegate).state = nullptr;
    impl_->view.navigationDelegate = nil;
    impl_->view.UIDelegate = nil;
    [impl_->view.configuration.userContentController removeScriptMessageHandlerForName:@(kBridgeName)];
    [impl_->view removeFromSuperview];
    impl_->container->syncNativeView = { };
    impl_->messageDelegate = nil;
    impl_->uiDelegate = nil;
    impl_->navigationDelegate = nil;
    impl_->view = nil;
}

bool WkWebView::isClosed() const { return impl_->state->lifetime.isClosed(); }

void WkWebView::sendMessage(const BridgeMessage& message, MessageCompletion completion)
{
    if (impl_->state->lifetime.isClosed()) {
        if (completion) {
            completion({ MessageError::Closed, QStringLiteral("The web view is closed.") });
        }
        return;
    }
    QString validationError;
    if (!impl_->state->policy->allowsBridge(impl_->state->committedUrl)
        || !impl_->state->policy->validateBridgeMessage(message, &validationError)) {
        if (completion) {
            completion({ MessageError::Rejected,
                validationError.isEmpty() ? QStringLiteral("The current document is not authorized for bridge messages.")
                                          : validationError });
        }
        return;
    }
    const QJsonObject envelope {
        { QStringLiteral("version"), message.version },
        { QStringLiteral("type"), message.type },
        { QStringLiteral("payload"), message.payload },
    };
    const auto generation = impl_->state->lifetime.token();
    const std::weak_ptr<NativeState> weakState = impl_->state;
    const auto completionHandler = ^(id, NSError* error) {
                         if (!completion) {
                             return;
                         }
                         const auto state = weakState.lock();
                         if (!state || state->lifetime.resultFor(generation) == MessageError::Closed) {
                             completion({ MessageError::Closed, QStringLiteral("The web view is closed.") });
                             return;
                         }
                         if (state->lifetime.resultFor(generation) == MessageError::NavigationChanged) {
                             completion({ MessageError::NavigationChanged,
                                 QStringLiteral("The document changed before message delivery completed.") });
                             return;
                         }
                         if (error) {
                             completion({ MessageError::Rejected, QString::fromUtf8(error.localizedDescription.UTF8String) });
                         } else {
                             completion({ });
                         }
                     };
    if (@available(macOS 11.0, *)) {
        [impl_->view callAsyncJavaScript:@"window.__systemWebViewReceive(message);"
                              arguments:@{ @"message" : foundationObject(envelope) }
                                inFrame:nil
                         inContentWorld:[WKContentWorld pageWorld]
                       completionHandler:completionHandler];
    } else if (completion) {
        completion({ MessageError::Unsupported,
            QStringLiteral("Native JavaScript argument binding requires macOS 11 or later.") });
    }
}

void WkWebView::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    if (!impl_->state->lifetime.isClosed()) {
        impl_->state->callbacks = std::move(callbacks);
    }
}

void* WkWebView::nativeConfigurationForTesting() const
{
    return impl_->view ? static_cast<void*>(impl_->view.configuration) : nullptr;
}
} // namespace webview
