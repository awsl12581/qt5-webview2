#pragma once

#include <QMainWindow>

#include "webview/IWebView.h"

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
};
} // namespace samples::demo
