#include "platform/macos/WkWebView.h"

#include "webview/JsonMessage.h"

#include <QEvent>
#include <QResizeEvent>
#include <QString>
#include <QTimer>
#include <QWidget>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

namespace {
constexpr auto kBridgeName = "systemWebView";

NSString* toNSString(const QString& value) { return [NSString stringWithUTF8String:value.toUtf8().constData()]; }

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
struct BridgeState {
    IWebView::MessageHandler handler;
    IWebView::NewWindowHandler newWindowHandler;
    std::function<void*(void*)> createWebView;
};

class WkWebView::Impl
{
public:
    NativeViewHost* container = nullptr;
    WKWebView* view = nil;
    BridgeState bridge;
    id delegate = nil;
};
} // namespace webview

@interface SystemWebViewMessageDelegate : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) webview::BridgeState* bridge;
@end

@interface SystemWebViewUIDelegate : NSObject <WKUIDelegate>
@property (nonatomic, assign) webview::BridgeState* bridge;
@end

@implementation SystemWebViewMessageDelegate
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message
{
    if (![message.name isEqualToString:@(kBridgeName)] || !self.bridge || !self.bridge->handler) {
        return;
    }
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (!data || error) {
        return;
    }
    QJsonObject object;
    if (webview::parseMessage(QString::fromUtf8(static_cast<const char*>(data.bytes), data.length), &object)) {
        self.bridge->handler(object);
    }
}
@end

@implementation SystemWebViewUIDelegate
- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)navigationAction
                    windowFeatures:(WKWindowFeatures*)windowFeatures
{
    if (!self.bridge || !self.bridge->createWebView) {
        return nil;
    }
    return static_cast<WKWebView*>(self.bridge->createWebView(configuration));
}
@end

namespace webview {
WkWebView::WkWebView(QWidget* parent)
    : impl_(std::make_unique<Impl>())
{
    initialize(nullptr);
    impl_->container->setParent(parent);
}

WkWebView::WkWebView(QWidget* parent, void* configuration)
    : impl_(std::make_unique<Impl>())
{
    initialize(configuration);
    impl_->container->setParent(parent);
}

void WkWebView::initialize(void* configuration)
{
    impl_->container = new NativeViewHost(nullptr);
    auto* content = [[WKUserContentController alloc] init];
    impl_->delegate = [[SystemWebViewMessageDelegate alloc] init];
    static_cast<SystemWebViewMessageDelegate*>(impl_->delegate).bridge = &impl_->bridge;
    [content addScriptMessageHandler:impl_->delegate name:@(kBridgeName)];

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
    uiDelegate.bridge = &impl_->bridge;
    impl_->delegate = uiDelegate;
    impl_->bridge.createWebView = [this](void* childConfiguration) -> void* {
        if (!impl_->bridge.newWindowHandler) {
            return nullptr;
        }
        auto child = std::unique_ptr<WkWebView>(new WkWebView(nullptr, childConfiguration));
        auto* nativeView = child->impl_->view;
        impl_->bridge.newWindowHandler(std::move(child));
        return nativeView;
    };
    impl_->view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1) configuration:nativeConfiguration];
    impl_->view.UIDelegate = uiDelegate;
    impl_->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    auto* hostView = reinterpret_cast<NSView*>(impl_->container->winId());
    impl_->container->syncNativeView = [hostView, view = impl_->view] {
        [hostView layoutSubtreeIfNeeded];
        view.frame = hostView.bounds;
    };
    [hostView addSubview:impl_->view];
    impl_->container->syncNativeView();
}

WkWebView::~WkWebView()
{
    [impl_->view.configuration.userContentController removeScriptMessageHandlerForName:@(kBridgeName)];
    [impl_->view removeFromSuperview];
    impl_->delegate = nil;
}

QWidget* WkWebView::widget() { return impl_->container; }

void WkWebView::load(const QUrl& url)
{
    [impl_->view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:toNSString(url.toString())]]];
}

void WkWebView::setHtml(const QString& html, const QUrl& baseUrl)
{
    [impl_->view loadHTMLString:toNSString(html) baseURL:[NSURL URLWithString:toNSString(baseUrl.toString())]];
}

void WkWebView::postMessage(const QJsonObject& message)
{
    const auto script = QStringLiteral("window.__systemWebViewReceive(%1);").arg(jsonForJavaScriptArgument(message));
    [impl_->view evaluateJavaScript:toNSString(script) completionHandler:nil];
}

void WkWebView::setMessageHandler(MessageHandler handler) { impl_->bridge.handler = std::move(handler); }

void WkWebView::setNewWindowHandler(NewWindowHandler handler) { impl_->bridge.newWindowHandler = std::move(handler); }
} // namespace webview
