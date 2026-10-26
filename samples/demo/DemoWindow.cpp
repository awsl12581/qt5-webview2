#include "DemoWindow.h"

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
namespace
{
class DemoPolicy final : public webview::WebViewPolicy
{
public:
    DemoPolicy()
        : WebViewPolicy([] {
            webview::WebViewPolicyConfig config;
            config.allowedAppHosts.insert("demo");
            config.pageToHostSchemas["hello"] = { { { "message", webview::JsonType::String } } };
            config.pageToHostSchemas["window"] = { { { "action", webview::JsonType::String } } };
            config.hostToPageSchemas["ack"] = { { { "message", webview::JsonType::String } } };
            config.hostToPageSchemas["window-state"] = { { { "maximized", webview::JsonType::Boolean } } };
            return config;
        }())
    {
    }

    webview::NewWindowDecision decideNewWindow(const webview::NewWindowRequest& request) const override
    {
        return request.url == webview::Url("https://example.com/") ? webview::NewWindowDecision::Allow
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
#ifdef Q_OS_WIN
    setWindowFlag(Qt::FramelessWindowHint);
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
    app.id = "demo";
    app.source = webview::LocalBundle { SYSTEM_WEBVIEW_RESOURCE_DIR, "demo.html" };
    app.bridgeAccess = webview::BridgeAccess::Allowed;
    const auto application = session_->createApplication(std::move(app));
    if (!application) {
        status_->setText(QStringLiteral("Failed to create demo application"));
        return;
    }
    auto webView = session_->createWebView();
    webView->open(application);
    addTab(std::move(webView), "Home");
}

void DemoWindow::addTab(webview::WebViewPtr webView, const std::string& title)
{
    auto* tab = new WebViewTab(std::move(webView), tabs_);
    auto* page = tab->webView();
    webview::WebViewHostCallbacks callbacks;
    callbacks.onLoad = [this](const webview::LoadEvent& event) {
        if (event.state == webview::LoadState::Failed) {
            status_->setText(QStringLiteral("Load failed: %1").arg(QString::fromStdString(event.error)));
        }
    };
    page->bridge().setEventHandler("window", [this, page](const webview::Json& payload) {
        const auto action = payload.at("action").get<std::string>();
        if (action == "drag" && windowHandle()) {
            windowHandle()->startSystemMove();
        }
        else if (action == "minimize") {
            showMinimized();
        }
        else if (action == "maximize") {
            const bool maximize = !isMaximized();
            maximize ? showMaximized() : showNormal();
            page->bridge().emitEvent("window-state", { { "maximized", maximize } });
        }
        else if (action == "close") {
            close();
        }
    });
    page->bridge().setEventHandler("hello", [this, page](const webview::Json& payload) {
        status_->setText(QStringLiteral("Page: %1").arg(QString::fromStdString(payload.at("message").get<std::string>())));
        page->bridge().emitEvent("ack", { { "message", "Native received your message." } });
    });
    callbacks.onNewWindow = [this](const webview::NewWindowRequest& request, webview::WebViewPtr child) {
        addTab(std::move(child), request.url.host().empty() ? "New tab" : request.url.host());
    };
    page->setHostCallbacks(std::move(callbacks));
    page->whenInitialized([this](const webview::InitializationResult& result) {
        if (result.state == webview::InitializationState::Failed) {
            status_->setText(QStringLiteral("Initialization failed: %1").arg(QString::fromStdString(result.error)));
        }
    });
    const int index = tabs_->addTab(tab, QString::fromStdString(title));
    tabs_->setCurrentIndex(index);
    tabs_->widget(index)->layout()->activate();
    page->attachNativeView();
}
} // namespace samples::demo
