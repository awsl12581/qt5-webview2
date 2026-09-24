#pragma once

#include <QIODevice>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace webview
{
struct PublishedResource
{
    QString token;
    QUrl url;
    QString mimeType;
    qint64 size = -1;
};

struct ResourceRequest
{
    QUrl url;
    QUrl sourceOrigin;
    QString documentToken;
    QString rangeHeader;
    qint64 rangeStart = -1;
    qint64 rangeEnd = -1;
};

struct ResourceResponse
{
    int status = 404;
    QString mimeType;
    qint64 totalSize = 0;
    qint64 offset = 0;
    qint64 length = 0;
    std::shared_ptr<void> lease;
    std::unique_ptr<QIODevice> body;
    bool acceptRanges = false;
};

class WebResourceManager final
{
public:
    explicit WebResourceManager(QUrl origin = { });
    ~WebResourceManager();

    PublishedResource publishFile(const QString& path, const QString& mimeType = { }, const QString& documentToken = { });
    ResourceResponse open(const ResourceRequest& request) const;
    void release(const QString& token);

private:
    friend class WebViewState;
    void setRevocationHandler(std::function<void(const QString&)> handler);
    void setContext(const QUrl& resourceOrigin, const QUrl& documentOrigin, const QString& documentToken);
    void setDocumentToken(const QString& documentToken);
    void releaseForDocument(const QString& documentToken);
    void releaseAll();

    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
