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

    auto webView = webview::createWebView();
    std::ifstream input(std::string(SYSTEM_WEBVIEW_RESOURCE_DIR) + "/demo.html");
    std::stringstream buffer;
    buffer << input.rdbuf();
    webView->setHtml(QString::fromStdString(buffer.str()), QUrl::fromLocalFile(QStringLiteral(SYSTEM_WEBVIEW_RESOURCE_DIR) + "/"));
    addTab(std::move(webView), QStringLiteral("Home"));
}

void DemoWindow::addTab(webview::WebViewPtr webView, const QString& title)
{
    auto* tab = new WebViewTab(std::move(webView), tabs_);
    auto* page = tab->webView();
    page->setMessageHandler([this, page](const QJsonObject& message) {
        status_->setText(QStringLiteral("Page: %1").arg(message.value("message").toString()));
        page->postMessage(QJsonObject { { "type", "ack" }, { "message", "Native received your message." } });
    });
    page->setNewWindowHandler([this](webview::WebViewPtr child) { addTab(std::move(child), QStringLiteral("New tab")); });
    const int index = tabs_->addTab(tab, title);
    tabs_->setCurrentIndex(index);
}
} // namespace samples::demo
