#include "internal/ResourceMapping.h"
#include "webview/PathSecurity.h"

#include <QDir>
#include <QFileInfo>
#include <unordered_set>

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

bool validateResourceMappings(std::vector<ResourceMapping>* mappings, std::string* error)
{
    if (!mappings) {
        if (error) {
            *error = "Resource mappings are unavailable.";
        }
        return false;
    }
    std::unordered_set<std::string> origins;
    for (auto& mapping : *mappings) {
        const auto normalized = normalizedOrigin(mapping.origin);
        const bool invalidOrigin = normalized.scheme() != QStringLiteral("app") || mapping.origin.port() >= 0
                                   || !mapping.origin.userInfo().isEmpty() || !mapping.origin.path().isEmpty() || mapping.origin.hasQuery()
                                   || mapping.origin.hasFragment();
        if (invalidOrigin) {
            if (error) {
                *error = "Invalid resource mapping origin: " + mapping.origin.toString().toStdString();
            }
            return false;
        }
        const auto originText = normalized.toString().toStdString();
        if (origins.contains(originText)) {
            if (error) {
                *error = "Duplicate resource mapping origin: " + originText;
            }
            return false;
        }
        const QFileInfo rootInfo(QString::fromStdString(mapping.localDirectory));
        const auto canonicalRoot = rootInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty() || !rootInfo.isDir()) {
            if (error) {
                *error = "Invalid resource mapping directory for " + originText + ": " + mapping.localDirectory;
            }
            return false;
        }
        origins.insert(originText);
        mapping.origin = normalized;
        mapping.localDirectory = canonicalRoot.toStdString();
    }
    return true;
}

const ResourceMapping* findResourceMapping(const std::vector<ResourceMapping>& mappings, const QUrl& url)
{
    const auto origin = normalizedOrigin(url);
    for (const auto& mapping : mappings) {
        if (mapping.origin == origin) {
            return &mapping;
        }
    }
    return nullptr;
}

std::string resolveMappedResource(const ResourceMapping& mapping, const QUrl& url, std::string* error, bool allowSpaFallback)
{
    const auto encodedPath = url.path(QUrl::FullyEncoded).toUtf8();
    const auto decodedPath = QUrl::fromPercentEncoding(encodedPath);
    const auto segments = decodedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral(".."))) {
        if (error) {
            *error = "The resource path escapes its mapping root.";
        }
        return { };
    }
    const auto resolve = [&mapping](const std::string& relativePath) {
        const QFileInfo candidate(QDir(QString::fromStdString(mapping.localDirectory)).filePath(QString::fromStdString(relativePath)));
        const auto canonicalPath = candidate.canonicalFilePath();
        const auto pathWithinRoot = QDir(QString::fromStdString(mapping.localDirectory)).relativeFilePath(canonicalPath);
        const bool outsideRoot = pathWithinRoot == QStringLiteral("..") || pathWithinRoot.startsWith(QStringLiteral("../"))
                                 || pathWithinRoot.startsWith(QStringLiteral("..\\"));
        return canonicalPath.isEmpty() || candidate.isDir() || outsideRoot || hasExternalFileLink(canonicalPath)
                   ? std::string()
                   : canonicalPath.toStdString();
    };
    const auto requestedPath = segments.join(QLatin1Char('/')).toStdString();
    const auto resolved = resolve(requestedPath);
    if (!resolved.empty()) {
        return resolved;
    }
    const QFileInfo requestInfo(QString::fromStdString(requestedPath));
    if (allowSpaFallback && mapping.spaFallback && !mapping.entryDocument.empty() && requestInfo.suffix().isEmpty()) {
        const auto entry = resolve(mapping.entryDocument);
        if (!entry.empty()) {
            return entry;
        }
    }
    {
        if (error) {
            *error = "The mapped resource is unavailable or outside its root.";
        }
        return { };
    }
}

std::string localBundleContentSecurityPolicy(ExternalNetworkAccess access)
{
    if (access == ExternalNetworkAccess::Allowed) {
        return { };
    }
    return std::string(
        "default-src 'self' data: blob:; connect-src 'self'; "
        "script-src 'self' 'unsafe-inline' 'unsafe-eval' blob:; "
        "style-src 'self' 'unsafe-inline'; object-src 'none'; "
        "base-uri 'self'; form-action 'self'");
}
} // namespace webview
