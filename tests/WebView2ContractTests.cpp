#include "webview/HostCompletion.h"
#include "webview/ResourceMapping.h"
#include "webview/WebViewFactory.h"
#include "webview/WebViewPolicy.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cassert>

namespace
{
void testPublicContracts()
{
    static_assert(sizeof(webview::DownloadRequest) > sizeof(QUrl));
    static_assert(sizeof(webview::FileSelectionRequest) >= sizeof(QUrl) * 2);

    QTemporaryDir root;
    assert(root.isValid());
    QVector<webview::WebResourceMapping> mappings {
        { QUrl(QStringLiteral("app://contract")), root.path() }
    };
    QString error;
    assert(webview::validateResourceMappings(&mappings, &error));

    const auto filePath = root.filePath(QStringLiteral("index.html"));
    QFile file(filePath);
    assert(file.open(QIODevice::WriteOnly));
    file.write("<!doctype html><title>contract</title>");
    file.close();
    assert(webview::resolveMappedResource(
               mappings.front(), QUrl(QStringLiteral("app://contract/index.html")), &error)
        == QFileInfo(filePath).canonicalFilePath());
    assert(webview::resolveMappedResource(
               mappings.front(), QUrl(QStringLiteral("app://contract/%2e%2e/secret")), &error)
        .isEmpty());
}

void testUnsupportedFileSelectionIsExplicit()
{
    auto session = webview::createWebViewSession({ });
    assert(session);
    assert(session->capabilitySupport(webview::WebViewCapability::FileSelection)
        == webview::CapabilitySupport::Unsupported);
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    testPublicContracts();
    testUnsupportedFileSelectionIsExplicit();
    return 0;
}
