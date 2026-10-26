#include "internal/ResourceMapping.h"
#include "webview/PathSecurity.h"

#include <QDir>
#include <QFileInfo>
#include <optional>
#include <unordered_set>

namespace webview
{
namespace
{
std::optional<std::string> decodePercentEncodedPath(std::string_view path)
{
    const auto digit = [](char value) -> int {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F') {
            return value - 'A' + 10;
        }
        return -1;
    };
    std::string decoded;
    decoded.reserve(path.size());
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == '%') {
            if (index + 2 >= path.size()) {
                return std::nullopt;
            }
            const int high = digit(path[index + 1]);
            const int low = digit(path[index + 2]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            const char value = static_cast<char>((high << 4) | low);
            if (value == '\0' || value == '\\') {
                return std::nullopt;
            }
            decoded.push_back(value);
            index += 2;
        }
        else if (path[index] == '\\') {
            return std::nullopt;
        }
        else {
            decoded.push_back(path[index]);
        }
    }
    return decoded;
}
}

Url normalizedOrigin(const Url& url)
{
    return url.origin();
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
        const bool invalidOrigin = !normalized.isValid() || normalized.scheme() != "app" || mapping.origin.port() >= 0
                                   || mapping.origin.hasCredentials() || (mapping.origin.path() != "" && mapping.origin.path() != "/")
                                   || mapping.origin.hasQuery() || mapping.origin.hasFragment();
        if (invalidOrigin) {
            if (error) {
                *error = "Invalid resource mapping origin: " + mapping.origin.toString();
            }
            return false;
        }
        const auto originText = normalized.toString();
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

const ResourceMapping* findResourceMapping(const std::vector<ResourceMapping>& mappings, const Url& url)
{
    const auto origin = normalizedOrigin(url);
    for (const auto& mapping : mappings) {
        if (mapping.origin == origin) {
            return &mapping;
        }
    }
    return nullptr;
}

std::string resolveMappedResource(const ResourceMapping& mapping, const Url& url, std::string* error, bool allowSpaFallback)
{
    const auto decodedPath = decodePercentEncodedPath(url.path());
    if (!decodedPath) {
        if (error) {
            *error = "The resource path contains invalid percent encoding.";
        }
        return {};
    }
    const auto resolve = [&mapping](const std::string& relativePath) {
        const QFileInfo candidate(QDir(QString::fromStdString(mapping.localDirectory)).filePath(QString::fromStdString(relativePath)));
        const auto canonicalPath = candidate.canonicalFilePath();
        const auto pathWithinRoot = QDir(QString::fromStdString(mapping.localDirectory)).relativeFilePath(canonicalPath);
        const bool outsideRoot = pathWithinRoot == QStringLiteral("..") || pathWithinRoot.startsWith(QStringLiteral("../"))
                                 || pathWithinRoot.startsWith(QStringLiteral("..\\"));
        return canonicalPath.isEmpty() || candidate.isDir() || outsideRoot
                       || hasExternalFileLink(std::filesystem::path(canonicalPath.toStdU16String()))
                   ? std::string()
                   : canonicalPath.toStdString();
    };
    const auto requestedPath = !decodedPath->empty() && decodedPath->front() == '/' ? decodedPath->substr(1) : *decodedPath;
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
        return {};
    }
}

std::string localBundleContentSecurityPolicy(ExternalNetworkAccess access)
{
    if (access == ExternalNetworkAccess::Allowed) {
        return {};
    }
    return std::string(
        "default-src 'self' data: blob:; connect-src 'self'; "
        "script-src 'self' 'unsafe-inline' 'unsafe-eval' blob:; "
        "style-src 'self' 'unsafe-inline'; object-src 'none'; "
        "base-uri 'self'; form-action 'self'");
}
} // namespace webview
