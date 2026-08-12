#include "webview/DocumentLifetime.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewPolicy.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>

int main()
{
    QJsonObject input { { "type", "ping" }, { "sequence", 7 } };
    QJsonObject output;
    assert(webview::parseMessage(webview::jsonForJavaScriptArgument(input), &output));
    assert(output == input);
    assert(!webview::parseMessage(QStringLiteral("[]"), &output));

    QTemporaryDir files;
    assert(files.isValid());
    QFile allowedFile(files.filePath(QStringLiteral("index.html")));
    assert(allowedFile.open(QIODevice::WriteOnly));
    allowedFile.close();

    webview::WebViewPolicyConfig config;
    config.allowedAppHosts.insert(QStringLiteral("ui"));
    config.allowedFileRoots.append(files.path());
    config.trustedHttpsOrigins.insert(QStringLiteral("https://trusted.example"));
    config.bridgeSchemas.insert(QStringLiteral("ping"), { QSet<QString> { QStringLiteral("sequence") } });
    config.maximumBridgeMessageBytes = 128;
    const webview::WebViewPolicy policy(std::move(config));

    const auto navigation = [&policy](const QString& url) {
        return policy.decideNavigation({ QUrl(url), true, true, false });
    };
    assert(navigation(QStringLiteral("https://example.com/page")) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("app://ui/home")) == webview::NavigationDecision::Allow);
    assert(navigation(QUrl::fromLocalFile(allowedFile.fileName()).toString()) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("app://other/home")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("javascript:alert(1)")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("custom://host/path")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("not a url")) == webview::NavigationDecision::Cancel);

    assert(policy.decideNewWindow({ QUrl(QStringLiteral("https://example.com")), true })
        == webview::NewWindowDecision::Cancel);
    assert(policy.decidePermission({ webview::PermissionKind::Camera, QUrl(QStringLiteral("app://ui")) })
        == webview::PermissionDecision::Deny);
    assert(policy.decideDownload({ QUrl(QStringLiteral("https://example.com/file")), { }, { } })
        == webview::DownloadDecision::Cancel);
    assert(policy.allowsBridge(QUrl(QStringLiteral("app://ui/page"))));
    assert(policy.allowsBridge(QUrl(QStringLiteral("https://trusted.example/path"))));
    assert(!policy.allowsBridge(QUrl(QStringLiteral("https://trusted.example.evil/path"))));

    QString validationError;
    const webview::BridgeMessage validMessage {
        1, QStringLiteral("ping"), QJsonObject { { QStringLiteral("sequence"), 1 } }
    };
    assert(policy.validateBridgeMessage(validMessage, &validationError));
    assert(validationError.isEmpty());
    assert(!policy.validateBridgeMessage({ 2, validMessage.type, validMessage.payload }, &validationError));
    assert(validationError.contains(QStringLiteral("version")));
    assert(!policy.validateBridgeMessage({ 1, QStringLiteral("unknown"), validMessage.payload }, &validationError));
    assert(validationError.contains(QStringLiteral("type")));
    assert(!policy.validateBridgeMessage({ 1, validMessage.type, { } }, &validationError));
    assert(validationError.contains(QStringLiteral("sequence")));
    assert(!policy.validateBridgeMessage(
        { 1, validMessage.type, QJsonObject { { QStringLiteral("sequence"), QString(200, QLatin1Char('x')) } } },
        &validationError));
    assert(validationError.contains(QStringLiteral("size")));

    webview::DocumentLifetime lifetime;
    const auto firstDocument = lifetime.token();
    assert(lifetime.resultFor(firstDocument) == webview::MessageError::None);
    lifetime.invalidate();
    assert(lifetime.resultFor(firstDocument) == webview::MessageError::NavigationChanged);
    const auto secondDocument = lifetime.token();
    assert(lifetime.resultFor(secondDocument) == webview::MessageError::None);
    lifetime.close();
    assert(lifetime.resultFor(secondDocument) == webview::MessageError::Closed);
    lifetime.close();
    assert(lifetime.resultFor(lifetime.token()) == webview::MessageError::Closed);
}
