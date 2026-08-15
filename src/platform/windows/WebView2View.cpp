#include "platform/windows/WebView2View.h"

#include "webview/WebViewState.h"

#include <QMetaObject>
#include <QPointer>
#include <QWidget>

namespace webview
{
namespace {
class NativeViewHost final : public QWidget
{
public:
    explicit NativeViewHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
    }
};
}

class WebView2View::Impl
{
public:
    explicit Impl(QWidget* parent, WebViewPolicyPtr policy)
        : container(new NativeViewHost(parent)), state(std::make_shared<WebViewState>()), policy(std::move(policy))
    {
        // TODO(webview2-controller): create CoreWebView2Controller after the session environment is Ready.
        state->failInitialization(QStringLiteral(
            "Windows WebView2 controller is not initialized in the static scaffold."));
    }

    NativeViewHost* container;
    std::shared_ptr<WebViewState> state;
    WebViewPolicyPtr policy;
    bool attached = false;
};

WebView2View::WebView2View(QWidget* parent, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>(parent, std::move(policy)))
{
}

WebView2View::~WebView2View() { close(); }
QWidget* WebView2View::widget() { return impl_->container; }
InitializationState WebView2View::initializationState() const { return impl_->state->initializationState(); }
void WebView2View::whenInitialized(InitializationCompletion completion) { impl_->state->whenInitialized(std::move(completion)); }
void WebView2View::attachNativeView() { impl_->attached = true; }
void WebView2View::detachNativeView() { impl_->attached = false; }

void WebView2View::load(const QUrl& url)
{
    impl_->state->runWhenReady([state = impl_->state, url](const InitializationResult& result) {
        if (result.state != InitializationState::Ready) {
            state->emitLoad(LoadState::Failed, ++state->navigationId, url, result.error);
        }
    });
}

void WebView2View::setHtml(const QString&, const QUrl& baseUrl) { load(baseUrl); }
void WebView2View::stop() {}
void WebView2View::reload() {}

void WebView2View::close()
{
    if (!impl_ || impl_->state->lifetime.isClosed()) {
        return;
    }
    impl_->attached = false;
    impl_->state->close();
}

bool WebView2View::isClosed() const { return impl_->state->lifetime.isClosed(); }

void WebView2View::sendMessage(const BridgeMessage&, MessageCompletion completion)
{
    if (completion) {
        completion({ MessageError::Unsupported, QStringLiteral("WebView2 controller is unavailable.") });
    }
}

void WebView2View::setHostCallbacks(WebViewHostCallbacks callbacks)
{
    impl_->state->callbacks = std::move(callbacks);
}
} // namespace webview
