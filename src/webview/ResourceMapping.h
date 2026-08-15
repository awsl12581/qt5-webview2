#pragma once

#include "webview/WebViewTypes.h"

namespace webview
{
QUrl normalizedOrigin(const QUrl& url);
bool validateResourceMappings(QVector<WebResourceMapping>* mappings, QString* error);
const WebResourceMapping* findResourceMapping(
    const QVector<WebResourceMapping>& mappings, const QUrl& url);
QString resolveMappedResource(const WebResourceMapping& mapping, const QUrl& url, QString* error);
} // namespace webview
