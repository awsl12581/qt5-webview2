#include "internal/Application.h"

#include <QFileInfo>
#include <utf8proc.h>

#include <algorithm>

namespace webview
{
namespace
{
bool validId(const std::string& text)
{
    if (text.empty()) {
        return false;
    }
    for (std::size_t offset = 0; offset < text.size();) {
        utf8proc_int32_t codepoint;
        const auto available = static_cast<utf8proc_ssize_t>(std::min<std::size_t>(text.size() - offset, 4));
        const auto consumed = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset), available, &codepoint);
        if (consumed <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(consumed);
        if (codepoint == '-') {
            continue;
        }
        switch (utf8proc_category(codepoint)) {
        case UTF8PROC_CATEGORY_LU:
        case UTF8PROC_CATEGORY_LL:
        case UTF8PROC_CATEGORY_LT:
        case UTF8PROC_CATEGORY_LM:
        case UTF8PROC_CATEGORY_LO:
        case UTF8PROC_CATEGORY_ND:
        case UTF8PROC_CATEGORY_NL:
        case UTF8PROC_CATEGORY_NO:
            break;
        default:
            return false;
        }
    }
    return true;
}
}

WebApplicationPtr createApplication(WebApplicationOptions options, std::string* error)
{
    if (!validId(options.id)) {
        if (error) {
            *error = "Application IDs must be nonempty UTF-8 containing only Unicode letters, numbers, and ASCII hyphens.";
        }
        return {};
    }
    if (const auto* bundle = std::get_if<LocalBundle>(&options.source)) {
        const QFileInfo root(QString::fromStdString(bundle->directory));
        const auto entryParts = QString::fromStdString(bundle->entryDocument).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (root.canonicalFilePath().isEmpty() || !root.isDir() || bundle->entryDocument.empty() || bundle->entryDocument.starts_with('/')
            || entryParts.contains(QStringLiteral(".."))) {
            if (error) {
                *error = "LocalBundle requires an existing directory and entry document.";
            }
            return {};
        }
        if (bundle->externalNetworkAccess != ExternalNetworkAccess::Denied
            && bundle->externalNetworkAccess != ExternalNetworkAccess::Allowed) {
            if (error) {
                *error = "LocalBundle external network access is invalid.";
            }
            return {};
        }
    }
    else {
        const bool developmentServer = std::holds_alternative<DevelopmentServer>(options.source);
        const Url& url = developmentServer ? std::get<DevelopmentServer>(options.source).url : std::get<RemoteOrigin>(options.source).url;
        const auto scheme = url.scheme();
        if (!url.isValid() || url.host().empty() || (developmentServer ? scheme != "http" && scheme != "https" : scheme != "https")
            || (url.path() != "/" && !url.path().empty()) || url.hasQuery() || url.hasFragment()) {
            if (error) {
                *error = "Application URL sources require an absolute origin URL.";
            }
            return {};
        }
    }
    auto application = std::make_shared<WebApplication>();
    application->id_ = std::move(options.id);
    application->source_ = std::move(options.source);
    application->bridgeAccess_ = options.bridgeAccess;
    return application;
}

Url WebApplication::origin() const
{
    if (std::holds_alternative<LocalBundle>(source_)) {
        return Url("app://" + id_);
    }
    return std::get_if<DevelopmentServer>(&source_) ? std::get<DevelopmentServer>(source_).url : std::get<RemoteOrigin>(source_).url;
}

Url WebApplication::urlForRoute(std::string_view route) const
{
    const auto path = route.empty() && std::holds_alternative<LocalBundle>(source_)
                          ? std::string_view(std::get<LocalBundle>(source_).entryDocument)
                          : route;
    if (path.empty()) {
        return origin();
    }
    const auto rootedPath = path.front() == '/' ? std::string(path) : "/" + std::string(path);
    return origin().withPath(rootedPath);
}
} // namespace webview
