#pragma once

#include <QMainWindow>

#include <string>

#include <system_webview/system_webview.h>

class QLabel;
class QTabWidget;
namespace webview { class QtHostWidget; }

namespace samples::demo
{
class DemoWindow final : public QMainWindow
{
public:
    DemoWindow();

private:
    void addTab(webview::WebViewPtr webView, const std::string& title, webview::QtHostWidget* host = nullptr);

    QTabWidget* tabs_ = nullptr;
    QLabel* status_ = nullptr;
    webview::QtHostWidget* pendingPopupHost_ = nullptr;
    webview::WebViewSessionPtr session_;
};
} // namespace samples::demo
