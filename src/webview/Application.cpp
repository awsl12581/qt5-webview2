#include "internal/Application.h"

#include <QFileInfo>

namespace webview
{
namespace {
bool validId(const QString& id)
{
    if (id.isEmpty()) return false;
    for (const auto character : id) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('-'))) return false;
    }
    return true;
}
}

WebApplicationPtr createApplication(WebApplicationOptions options, QString* error)
{
    if (!validId(options.id)) {
        if (error) *error = QStringLiteral("Application IDs must contain only letters, digits, and hyphens.");
        return { };
    }
    if (const auto* bundle = std::get_if<LocalBundle>(&options.source)) {
        const QFileInfo root(bundle->directory);
        const auto entryParts = bundle->entryDocument.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (root.canonicalFilePath().isEmpty() || !root.isDir() || bundle->entryDocument.isEmpty()
            || bundle->entryDocument.startsWith(QLatin1Char('/')) || entryParts.contains(QStringLiteral(".."))) {
            if (error) *error = QStringLiteral("LocalBundle requires an existing directory and entry document.");
            return { };
        }
    } else {
        const bool developmentServer = std::holds_alternative<DevelopmentServer>(options.source);
        const QUrl url = developmentServer ? std::get<DevelopmentServer>(options.source).url
                                           : std::get<RemoteOrigin>(options.source).url;
        const auto scheme = url.scheme().toLower();
        if (!url.isValid() || url.host().isEmpty() || (developmentServer
                ? scheme != QStringLiteral("http") && scheme != QStringLiteral("https")
                : scheme != QStringLiteral("https"))
            || (url.path() != QStringLiteral("/") && !url.path().isEmpty()) || url.hasQuery()
            || url.hasFragment()) {
            if (error) *error = QStringLiteral("Application URL sources require an absolute origin URL.");
            return { };
        }
    }
    auto application = std::make_shared<WebApplication>();
    application->id_ = std::move(options.id);
    application->source_ = std::move(options.source);
    application->bridgeAccess_ = options.bridgeAccess;
    return application;
}

QUrl WebApplication::origin() const
{
    if (std::holds_alternative<LocalBundle>(source_)) return QUrl(QStringLiteral("app://%1").arg(id_));
    return std::get_if<DevelopmentServer>(&source_) ? std::get<DevelopmentServer>(source_).url
        : std::get<RemoteOrigin>(source_).url;
}

QUrl WebApplication::urlForRoute(const QString& route) const
{
    QUrl url = origin();
    const auto cleanRoute = route.isEmpty()
        ? (std::holds_alternative<LocalBundle>(source_) ? std::get<LocalBundle>(source_).entryDocument : QString())
        : route;
    url.setPath(cleanRoute.startsWith(QLatin1Char('/')) ? cleanRoute : QStringLiteral("/") + cleanRoute);
    return url;
}
} // namespace webview
