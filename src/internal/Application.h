#pragma once

#include "webview/WebViewTypes.h"

namespace webview
{
class WebApplication
{
public:
    const QString& id() const { return id_; }
    const ApplicationSource& source() const { return source_; }
    BridgeAccess bridgeAccess() const { return bridgeAccess_; }
    QUrl origin() const;
    QUrl urlForRoute(const QString& route) const;

private:
    friend WebApplicationPtr createApplication(WebApplicationOptions options, QString* error);
    QString id_;
    ApplicationSource source_;
    BridgeAccess bridgeAccess_ = BridgeAccess::Denied;
};

WebApplicationPtr createApplication(WebApplicationOptions options, QString* error);
} // namespace webview
