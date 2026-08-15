#include "webview/ResourceMapping.h"
#include "webview/PathSecurity.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace webview
{
QUrl normalizedOrigin(const QUrl& url)
{
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        return { };
    }
    QUrl origin;
    origin.setScheme(url.scheme().toLower());
    origin.setHost(url.host().toLower());
    if (url.port() >= 0) {
        origin.setPort(url.port());
    }
    return origin;
}

bool validateResourceMappings(QVector<WebResourceMapping>* mappings, QString* error)
{
    if (!mappings) {
        if (error) {
            *error = QStringLiteral("Resource mappings are unavailable.");
        }
        return false;
    }
    QSet<QString> origins;
    for (auto& mapping : *mappings) {
        const auto normalized = normalizedOrigin(mapping.origin);
        const bool invalidOrigin = normalized.scheme() != QStringLiteral("app")
            || mapping.origin.port() >= 0 || !mapping.origin.userInfo().isEmpty()
            || !mapping.origin.path().isEmpty() || mapping.origin.hasQuery()
            || mapping.origin.hasFragment();
        if (invalidOrigin) {
            if (error) {
                *error = QStringLiteral("Invalid resource mapping origin: %1").arg(mapping.origin.toString());
            }
            return false;
        }
        const auto originText = normalized.toString();
        if (origins.contains(originText)) {
            if (error) {
                *error = QStringLiteral("Duplicate resource mapping origin: %1").arg(originText);
            }
            return false;
        }
        const QFileInfo rootInfo(mapping.localDirectory);
        const auto canonicalRoot = rootInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
            if (error) {
                *error = QStringLiteral("Invalid resource mapping directory for %1: %2")
                             .arg(originText, mapping.localDirectory);
            }
            return false;
        }
        origins.insert(originText);
        mapping.origin = normalized;
        mapping.localDirectory = canonicalRoot;
    }
    return true;
}

const WebResourceMapping* findResourceMapping(
    const QVector<WebResourceMapping>& mappings, const QUrl& url)
{
    const auto origin = normalizedOrigin(url);
    for (const auto& mapping : mappings) {
        if (mapping.origin == origin) {
            return &mapping;
        }
    }
    return nullptr;
}

QString resolveMappedResource(const WebResourceMapping& mapping, const QUrl& url, QString* error)
{
    const auto encodedPath = url.path(QUrl::FullyEncoded).toUtf8();
    const auto decodedPath = QUrl::fromPercentEncoding(encodedPath);
    const auto segments = decodedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral(".."))) {
        if (error) {
            *error = QStringLiteral("The resource path escapes its mapping root.");
        }
        return { };
    }
    const auto candidatePath = QDir(mapping.localDirectory).filePath(segments.join(QLatin1Char('/')));
    const QFileInfo candidate(candidatePath);
    const auto canonicalPath = candidate.canonicalFilePath();
    const auto relativePath = QDir(mapping.localDirectory).relativeFilePath(canonicalPath);
    const bool outsideRoot = relativePath == QStringLiteral("..")
        || relativePath.startsWith(QStringLiteral("../"))
        || relativePath.startsWith(QStringLiteral("..\\"));
    if (canonicalPath.isEmpty() || candidate.isDir() || outsideRoot
        || hasExternalFileLink(canonicalPath)) {
        if (error) {
            *error = QStringLiteral("The mapped resource is unavailable or outside its root.");
        }
        return { };
    }
    return canonicalPath;
}
} // namespace webview
