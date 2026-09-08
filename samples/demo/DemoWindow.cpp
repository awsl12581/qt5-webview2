#include "DemoWindow.h"

#include "webview/WebViewFactory.h"

#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

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
            config.pageToHostSchemas.insert(QStringLiteral("hello"),
                { { { QStringLiteral("message"), QJsonValue::String } } });
            config.pageToHostSchemas.insert(QStringLiteral("window"),
                { { { QStringLiteral("action"), QJsonValue::String } } });
            config.hostToPageSchemas.insert(QStringLiteral("ack"),
                { { { QStringLiteral("message"), QJsonValue::String } } });
            config.hostToPageSchemas.insert(QStringLiteral("window-state"),
                { { { QStringLiteral("maximized"), QJsonValue::Bool } } });
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
    setWindowFlag(Qt::FramelessWindowHint);
#ifdef Q_OS_WIN
    const auto hwnd = reinterpret_cast<HWND>(winId());
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    const MARGINS shadow { 1, 1, 1, 1 };
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
#endif
    setWindowTitle(QStringLiteral("System WebView Demo"));
    resize(1000, 700);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setTabBarAutoHide(true);
    tabs_->setTabsClosable(true);
    tabs_->setStyleSheet(QStringLiteral("QTabWidget::pane { border: 0; }"));
    connect(tabs_, &QTabWidget::tabCloseRequested, tabs_, [this](int index) {
        if (tabs_->count() > 1) {
            delete tabs_->widget(index);
        }
    });
    setCentralWidget(tabs_);
    status_ = new QLabel(QStringLiteral("Waiting for page message"), this);
    statusBar()->addWidget(status_);
    statusBar()->hide();

    webview::WebViewSessionOptions options;
    options.mode = webview::SessionMode::Ephemeral;
    session_ = webview::createWebViewSession(std::move(options), std::make_shared<DemoPolicy>());
    webview::WebApplicationOptions app;
    app.id = QStringLiteral("demo");
    app.source = webview::LocalBundle {
        QString::fromUtf8(SYSTEM_WEBVIEW_RESOURCE_DIR), QStringLiteral("demo.html")
    };
    app.bridgeAccess = webview::BridgeAccess::Allowed;
    const auto application = session_->createApplication(std::move(app));
    if (!application) {
        status_->setText(QStringLiteral("Failed to create demo application"));
        return;
    }
    auto webView = session_->createWebView();
    webView->open(application);
    addTab(std::move(webView), QStringLiteral("Home"));
}

void DemoWindow::addTab(webview::WebViewPtr webView, const QString& title)
{
    auto* tab = new WebViewTab(std::move(webView), tabs_);
    auto* page = tab->webView();
    webview::WebViewHostCallbacks callbacks;
    callbacks.load = [this](const webview::LoadEvent& event) {
        if (event.state == webview::LoadState::Failed) {
            status_->setText(QStringLiteral("Load failed: %1").arg(event.error));
        }
    };
    callbacks.message = [this, page](const webview::BridgeMessage& message) {
        if (message.type == QStringLiteral("window")) {
            const auto action = message.payload.value(QStringLiteral("action")).toString();
            if (action == QStringLiteral("drag") && windowHandle()) {
                windowHandle()->startSystemMove();
            } else if (action == QStringLiteral("minimize")) {
                showMinimized();
            } else if (action == QStringLiteral("maximize")) {
                const bool maximize = !isMaximized();
                maximize ? showMaximized() : showNormal();
                page->sendMessage({ 1, QStringLiteral("window-state"), QJsonObject { { "maximized", maximize } } });
            } else if (action == QStringLiteral("close")) {
                close();
            }
            return;
        }
        status_->setText(QStringLiteral("Page: %1").arg(message.payload.value("message").toString()));
        page->sendMessage({ 1, QStringLiteral("ack"), QJsonObject { { "message", "Native received your message." } } });
    };
    callbacks.newWindow = [this](const webview::NewWindowRequest& request, webview::WebViewPtr child) {
        addTab(std::move(child), request.url.host().isEmpty() ? QStringLiteral("New tab") : request.url.host());
    };
    page->setHostCallbacks(std::move(callbacks));
    page->whenInitialized([this](const webview::InitializationResult& result) {
        if (result.state == webview::InitializationState::Failed) {
            status_->setText(QStringLiteral("Initialization failed: %1").arg(result.error));
        }
    });
    const int index = tabs_->addTab(tab, title);
    tabs_->setCurrentIndex(index);
    tabs_->widget(index)->layout()->activate();
    page->attachNativeView();
}
} // namespace samples::demo
