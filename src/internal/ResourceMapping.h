#pragma once

#include "webview/WebViewTypes.h"

namespace webview
{
struct ResourceMapping {
    QUrl origin;
    QString localDirectory;
    QString entryDocument;
    bool spaFallback = false;
};

QUrl normalizedOrigin(const QUrl& url);
bool validateResourceMappings(QVector<ResourceMapping>* mappings, QString* error);
const ResourceMapping* findResourceMapping(const QVector<ResourceMapping>& mappings, const QUrl& url);
QString resolveMappedResource(const ResourceMapping& mapping, const QUrl& url, QString* error,
    bool allowSpaFallback = false);
} // namespace webview
