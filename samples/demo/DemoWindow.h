#pragma once

#include <QMainWindow>

#include <system_webview/system_webview.h>

class QLabel;
class QTabWidget;

namespace samples::demo
{
class DemoWindow final : public QMainWindow
{
public:
    DemoWindow();

private:
    void addTab(webview::WebViewPtr webView, const QString& title);

    QTabWidget* tabs_ = nullptr;
    QLabel* status_ = nullptr;
    webview::WebViewSessionPtr session_;
};
} // namespace samples::demo
