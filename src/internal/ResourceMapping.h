#pragma once

#include <system_webview/system_webview.h>

namespace webview
{
struct ResourceMapping
{
    QUrl origin;
    QString localDirectory;
    QString entryDocument;
    bool spaFallback = false;
    ExternalNetworkAccess externalNetworkAccess = ExternalNetworkAccess::Denied;
};

QUrl normalizedOrigin(const QUrl& url);
bool validateResourceMappings(QVector<ResourceMapping>* mappings, QString* error);
const ResourceMapping* findResourceMapping(const QVector<ResourceMapping>& mappings, const QUrl& url);
QString resolveMappedResource(const ResourceMapping& mapping, const QUrl& url, QString* error, bool allowSpaFallback = false);
QString localBundleContentSecurityPolicy(ExternalNetworkAccess access);
} // namespace webview
