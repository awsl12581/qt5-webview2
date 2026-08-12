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
    WebViewHostCallbacks callbacks;
    std::function<void*(void*)> createWebView;
};

class WkWebView::Impl
{
public:
    NativeViewHost* container = nullptr;
    WKWebView* view = nil;
    BridgeState bridge;
    WebViewPolicyPtr policy;
    id messageDelegate = nil;
    id uiDelegate = nil;
    bool closed = false;
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
    if (![message.name isEqualToString:@(kBridgeName)] || !self.bridge || !self.bridge->callbacks.message) {
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
        bridgeMessage.version = object.value(QStringLiteral("version")).toInt(1);
        bridgeMessage.type = object.value(QStringLiteral("type")).toString();
        bridgeMessage.payload = object.value(QStringLiteral("payload")).toObject();
        self.bridge->callbacks.message(bridgeMessage);
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
    impl_->policy = std::move(policy);
    impl_->container = new NativeViewHost(nullptr);
    auto* content = [[WKUserContentController alloc] init];
    impl_->messageDelegate = [[SystemWebViewMessageDelegate alloc] init];
    static_cast<SystemWebViewMessageDelegate*>(impl_->messageDelegate).bridge = &impl_->bridge;
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
    uiDelegate.bridge = &impl_->bridge;
    impl_->uiDelegate = uiDelegate;
    impl_->bridge.createWebView = [this](void* childConfiguration) -> void* {
        if (!impl_->bridge.callbacks.newWindow) {
            return nullptr;
        }
        auto child = std::unique_ptr<WkWebView>(new WkWebView(nullptr, childConfiguration, impl_->policy));
        auto* nativeView = child->impl_->view;
        impl_->bridge.callbacks.newWindow(NewWindowRequest { }, std::move(child));
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

WkWebView::~WkWebView() { close(); }

QWidget* WkWebView::widget() { return impl_->container; }

void WkWebView::load(const QUrl& url)
{
    if (impl_->closed) {
        return;
    }
    [impl_->view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:toNSString(url.toString())]]];
}

void WkWebView::setHtml(const QString& html, const QUrl& baseUrl)
{
    if (impl_->closed) {
        return;
    }
    [impl_->view loadHTMLString:toNSString(html) baseURL:[NSURL URLWithString:toNSString(baseUrl.toString())]];
}

void WkWebView::stop()
{
    if (!impl_->closed) {
        [impl_->view stopLoading];
    }
}

void WkWebView::reload()
{
    if (!impl_->closed) {
        [impl_->view reload];
    }
}

void WkWebView::close()
{
    if (impl_->closed) {
        return;
    }
    impl_->closed = true;
    impl_->bridge.callbacks = { };
    [impl_->view stopLoading];
    impl_->view.navigationDelegate = nil;
    impl_->view.UIDelegate = nil;
    [impl_->view.configuration.userContentController removeScriptMessageHandlerForName:@(kBridgeName)];
    [impl_->view removeFromSuperview];
    impl_->container->syncNativeView = { };
    impl_->messageDelegate = nil;
    impl_->uiDelegate = nil;
    impl_->view = nil;
}

bool WkWebView::isClosed() const { return impl_->closed; }

void WkWebView::sendMessage(const BridgeMessage& message, MessageCompletion completion)
{
    if (impl_->closed) {
        if (completion) {
            completion({ MessageError::Closed, QStringLiteral("The web view is closed.") });
        }
        return;
    }
    const QJsonObject envelope {
        { QStringLiteral("version"), message.version },
        { QStringLiteral("type"), message.type },
        { QStringLiteral("payload"), message.payload },
    };
    const auto script = QStringLiteral("window.__systemWebViewReceive(%1);").arg(jsonForJavaScriptArgument(envelope));
    [impl_->view evaluateJavaScript:toNSString(script)
                     completionHandler:^(id, NSError* error) {
                         if (!completion) {
                             return;
                         }
                         if (error) {
                             completion({ MessageError::Rejected, QString::fromUtf8(error.localizedDescription.UTF8String) });
                         } else {
                             completion({ });
                         }
                     }];
}

void WkWebView::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    if (!impl_->closed) {
        impl_->bridge.callbacks = std::move(callbacks);
    }
}
} // namespace webview
