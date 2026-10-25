#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <ada.h>

#include <iostream>
#include <string>
#include <string_view>

namespace
{
QString text(std::string_view input)
{
    return QString::fromUtf8(input.data(), static_cast<int>(input.size()));
}

void emitRow(const QJsonObject& row)
{
    std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).toStdString() << '\n';
}

// Same component reconstruction as src/webview/ResourceMapping.cpp.
QUrl projectOrigin(const QUrl& url)
{
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        return {};
    }
    QUrl origin;
    origin.setScheme(url.scheme().toLower());
    origin.setHost(url.host().toLower());
    if (url.port() >= 0) {
        origin.setPort(url.port());
    }
    return origin;
}
}

int main()
{
    emitRow({ { "kind", "environment" }, { "qt", qVersion() }, { "ada", ADA_VERSION }, { "cplusplus", static_cast<double>(__cplusplus) } });
    for (const auto input : { "https://EXAMPLE.com:443/a?x#y",
                              "http://example.com:80",
                              "https://例子.测试/数据.txt",
                              "https://user:pass@example.com/a",
                              "https://[::1]:443/a",
                              "https://example.com?",
                              "https://example.com#",
                              "app://demo",
                              "app://DEMO",
                              "APP://DEMO/a",
                              "app://另一个/数据.txt",
                              "app://demo/",
                              "app://user:pass@demo:80/a",
                              "app://demo/%2e%2e/secret",
                              "app://demo/a/../secret",
                              "app://demo/%2Fetc",
                              "app://demo/%5Csecret",
                              "app://demo/%252e%252e/secret",
                              "app://demo/%00x",
                              "app://demo/%FF",
                              "app://demo/%zz",
                              "app:demo",
                              "resource/token",
                              "//example.com/x",
                              "javascript:alert(1)",
                              "about:blank",
                              "data:text/plain,hello",
                              "file:///tmp/数据%20x.txt",
                              "file:///C:/Data/a%20b.txt",
                              "file://server/share/a.txt",
                              "http://127.1/",
                              "https://example.com\\a",
                              " https://example.com/ ",
                              "https://exa\nmple.com/" }) {
        const QUrl qt(text(input));
        auto ada = ada::parse<ada::url_aggregator>(input);
        QJsonObject row { { "kind", "parse" },
                          { "input", text(input) },
                          { "qt_valid", qt.isValid() },
                          { "qt_relative", qt.isRelative() },
                          { "qt_href", qt.toString(QUrl::FullyEncoded) },
                          { "qt_host", qt.host() },
                          { "qt_port", qt.port() },
                          { "qt_origin", projectOrigin(qt).toString(QUrl::FullyEncoded) },
                          { "qt_adjusted",
                            qt.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString(QUrl::FullyEncoded) },
                          { "qt_path", qt.path() },
                          { "qt_encoded_path", qt.path(QUrl::FullyEncoded) },
                          { "qt_once_decoded", QUrl::fromPercentEncoding(qt.path(QUrl::FullyEncoded).toUtf8()) },
                          { "qt_file", qt.toLocalFile() },
                          { "ada_valid", static_cast<bool>(ada) } };
        if (ada) {
            row.insert("ada_href", text(ada->get_href()));
            row.insert("ada_host", text(ada->get_hostname()));
            row.insert("ada_port", text(ada->get_port()));
            row.insert("ada_origin", text(ada->get_origin()));
            row.insert("ada_path", text(ada->get_pathname()));
            row.insert("ada_has_query", ada->has_search());
            row.insert("ada_has_fragment", ada->has_hash());
        }
        emitRow(row);
    }
    for (const auto route : { "订单/详情", "a%2Fb", "a?b#c", "../secret", "//other/x" }) {
        QUrl qt(QStringLiteral("app://demo"));
        const std::string path = std::string(route).starts_with('/') ? route : "/" + std::string(route);
        qt.setPath(text(path));
        auto ada = ada::parse("app://demo");
        const bool success = ada->set_pathname(path);
        emitRow(
            { { "kind", "route" },
              { "input", text(route) },
              { "qt_href", qt.toString(QUrl::FullyEncoded) },
              { "ada_href", text(ada->get_href()) },
              { "ada_set_success", success } });
    }
    for (const auto base : { "app://demo", "app://demo/", "https://example.com/sub/" }) {
        const QUrl qt(text(base));
        auto adaBase = ada::parse(base);
        auto ada = ada::parse("resource/token", &*adaBase);
        emitRow(
            { { "kind", "resolve" },
              { "base", text(base) },
              { "qt_href", qt.resolved(QUrl(QStringLiteral("resource/token"))).toString(QUrl::FullyEncoded) },
              { "ada_href", ada ? text(ada->get_href()) : QStringLiteral("parse-failed") } });
    }
    for (const auto path : { "/tmp/数据 x#%.txt", "C:\\Data\\a b.txt", "\\\\server\\share\\a b.txt", "/tmp/a\nb.txt" }) {
        emitRow(
            { { "kind", "file" },
              { "input", text(path) },
              { "qt_href", QUrl::fromLocalFile(text(path)).toString(QUrl::FullyEncoded) },
              { "ada_href", text(ada::href_from_file(path)) } });
    }
}
