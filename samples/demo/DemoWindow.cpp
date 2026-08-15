#include "DemoWindow.h"

#include "webview/WebViewFactory.h"

#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <fstream>
#include <sstream>

namespace samples::demo
{
namespace {
class DemoPolicy final : public webview::WebViewPolicy
{
public:
    DemoPolicy()
        : WebViewPolicy([] {
            webview::WebViewPolicyConfig config;
            config.allowedAppHosts.insert(QStringLiteral("demo"));
            config.bridgeSchemas.insert(
                QStringLiteral("hello"), { QSet<QString> { QStringLiteral("message") } });
            config.bridgeSchemas.insert(
                QStringLiteral("ack"), { QSet<QString> { QStringLiteral("message") } });
            return config;
        }())
    {
    }

    webview::NewWindowDecision decideNewWindow(const webview::NewWindowRequest& request) const override
    {
        return request.url == QUrl(QStringLiteral("https://example.com/"))
            ? webview::NewWindowDecision::Allow
            : webview::NewWindowDecision::Cancel;
    }
};
}

class WebViewTab final : public QWidget
{
public:
    explicit WebViewTab(webview::WebViewPtr webView, QWidget* parent)
        : QWidget(parent)
        , webView_(std::move(webView))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(webView_->widget());
    }

    ~WebViewTab() override
    {
        webView_->detachNativeView();
        webView_->close();
    }

    webview::IWebView* webView() { return webView_.get(); }

private:
    webview::WebViewPtr webView_;
};

DemoWindow::DemoWindow()
{
    setWindowTitle(QStringLiteral("System WebView Demo"));
    resize(1000, 700);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, tabs_, [this](int index) {
        if (tabs_->count() > 1) {
            delete tabs_->widget(index);
        }
    });
    setCentralWidget(tabs_);
    status_ = new QLabel(QStringLiteral("Waiting for page message"), this);
    statusBar()->addWidget(status_);

    webview::WebViewSessionOptions options;
    options.resourceMappings.push_back({
        QUrl(QStringLiteral("app://demo")),
        QString::fromUtf8(SYSTEM_WEBVIEW_RESOURCE_DIR)
    });
    session_ = webview::createWebViewSession(std::move(options), std::make_shared<DemoPolicy>());
    auto webView = session_->createWebView();
    std::ifstream input(std::string(SYSTEM_WEBVIEW_RESOURCE_DIR) + "/demo.html");
    std::stringstream buffer;
    buffer << input.rdbuf();
    webView->setHtml(QString::fromStdString(buffer.str()), QUrl(QStringLiteral("app://demo/index.html")));
    addTab(std::move(webView), QStringLiteral("Home"));
}

void DemoWindow::addTab(webview::WebViewPtr webView, const QString& title)
{
    auto* tab = new WebViewTab(std::move(webView), tabs_);
    auto* page = tab->webView();
    webview::WebViewHostCallbacks callbacks;
    callbacks.message = [this, page](const webview::BridgeMessage& message) {
        status_->setText(QStringLiteral("Page: %1").arg(message.payload.value("message").toString()));
        page->sendMessage({ 1, QStringLiteral("ack"), QJsonObject { { "message", "Native received your message." } } });
    };
    callbacks.newWindow = [this](const webview::NewWindowRequest& request, webview::WebViewPtr child) {
        addTab(std::move(child), request.url.host().isEmpty() ? QStringLiteral("New tab") : request.url.host());
    };
    page->setHostCallbacks(std::move(callbacks));
    const int index = tabs_->addTab(tab, title);
    tabs_->setCurrentIndex(index);
    tabs_->widget(index)->layout()->activate();
    page->attachNativeView();
}
} // namespace samples::demo
