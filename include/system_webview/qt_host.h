#pragma once

#include <system_webview/system_webview.h>

#include <QMetaObject>
#include <QResizeEvent>
#include <QThread>
#include <QWidget>

#include <cstdint>

namespace webview
{
/// Qt host widget adapter. The widget is owned by the Qt application.
class QtHostWidget final : public QWidget
{
public:
    explicit QtHostWidget(QWidget* parent = nullptr)
        : QWidget(parent)
        , uiThread_(QThread::currentThread())
    {
        setAttribute(Qt::WA_NativeWindow);
    }

    WebViewHost webViewHost()
    {
        WebViewHost host;
        host.nativeHandle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(winId()));
        host.width = width();
        host.height = height();
        host.visible = isVisible();
        host.postToUi = [this](std::function<void()> action) {
            if (QThread::currentThread() == uiThread_) {
                if (action) {
                    action();
                }
                return;
            }
            QMetaObject::invokeMethod(this, std::move(action), Qt::QueuedConnection);
        };
        host.observeChanges = [this](std::function<void()> callback) { changed_ = std::move(callback); };
        return host;
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        notifyChanged();
    }

    bool event(QEvent* event) override
    {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest || event->type() == QEvent::PolishRequest) {
            notifyChanged();
        }
        return handled;
    }

private:
    void notifyChanged()
    {
        if (changed_) {
            changed_();
        }
    }

    QThread* uiThread_ = nullptr;
    std::function<void()> changed_;
};
} // namespace webview
