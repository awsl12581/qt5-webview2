#pragma once

#include <system_webview/system_webview.h>

#include <functional>
#include <utility>

namespace webview
{
/// Framework-neutral host boundary used by platform backends. This header must
/// remain independent of GUI toolkit headers; toolkit adapters implement it
/// through WebViewHost.
class HostView
{
public:
    using Callback = std::function<void()>;

    virtual ~HostView() = default;
    virtual void* nativeHandle() const = 0;
    virtual bool hasVisibleGeometry() const = 0;
    virtual bool postToUi(Callback action) = 0;
    virtual void setHostChangedCallback(Callback callback) = 0;
    virtual void refreshHost() = 0;
};

class ExternalHostView final : public HostView
{
public:
    explicit ExternalHostView(WebViewHost host)
        : host_(std::move(host))
    {
    }

    void* nativeHandle() const override
    {
        refreshHostState();
        return host_.nativeHandle;
    }
    bool hasVisibleGeometry() const override
    {
        refreshHostState();
        return host_.visible && host_.width > 0 && host_.height > 0;
    }
    bool postToUi(Callback action) override
    {
        if (!host_.postToUi) {
            if (action) {
                action();
            }
            return static_cast<bool>(action);
        }
        host_.postToUi(std::move(action));
        return true;
    }
    void setHostChangedCallback(Callback callback) override
    {
        hostChanged_ = std::move(callback);
        if (host_.observeChanges) {
            host_.observeChanges([this] {
                if (hostChanged_) {
                    hostChanged_();
                }
            });
        }
    }
    void refreshHost() override
    {
        refreshHostState();
        if (hostChanged_) {
            hostChanged_();
        }
    }

private:
    void refreshHostState() const
    {
        if (host_.refresh) {
            host_.refresh(host_);
        }
    }

    mutable WebViewHost host_;
    Callback hostChanged_;
};

} // namespace webview
