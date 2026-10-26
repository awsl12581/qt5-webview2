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

    void* nativeHandle() const override { return host_.nativeHandle; }
    bool hasVisibleGeometry() const override { return host_.visible && host_.width > 0 && host_.height > 0; }
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
        if (host_.observeChanges) {
            host_.observeChanges(std::move(callback));
        }
    }
    void refreshHost() override {}

private:
    WebViewHost host_;
};

} // namespace webview
