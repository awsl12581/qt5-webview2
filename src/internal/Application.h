#pragma once

#include <system_webview/system_webview.h>

#include <string>
#include <string_view>

namespace webview
{
class WebApplication
{
public:
    const std::string& id() const { return id_; }

    const ApplicationSource& source() const { return source_; }

    BridgeAccess bridgeAccess() const { return bridgeAccess_; }

    QUrl origin() const;
    QUrl urlForRoute(std::string_view route) const;

private:
    friend WebApplicationPtr createApplication(WebApplicationOptions options, std::string* error);
    std::string id_;
    ApplicationSource source_;
    BridgeAccess bridgeAccess_ = BridgeAccess::Denied;
};

WebApplicationPtr createApplication(WebApplicationOptions options, std::string* error);
} // namespace webview
