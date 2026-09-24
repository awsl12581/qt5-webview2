#include "internal/Diagnostics.h"
#include <system_webview/system_webview.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <QHash>

#include <memory>
#include <mutex>
#include <utility>

namespace webview
{
namespace
{
constexpr qint64 kMaximumFileSize = 2LL * 1024 * 1024 * 1024;
constexpr qint64 kMaximumPublishedBytes = 8LL * 1024 * 1024 * 1024;
constexpr int kMaximumResourceCount = 128;
constexpr qint64 kCopyBufferSize = 1024 * 1024;
constexpr qint64 kResourceLifetimeMs = 30LL * 60 * 1000;

class RangeFile final : public QFile
{
public:
    RangeFile(const QString& path, qint64 length)
        : QFile(path)
        , remaining_(length)
    {
    }

protected:
    qint64 readData(char* data, qint64 maximum) override
    {
        if (remaining_ <= 0) {
            return 0;
        }
        const auto count = QFile::readData(data, qMin(maximum, remaining_));
        if (count > 0) {
            remaining_ -= count;
        }
        return count;
    }

private:
    qint64 remaining_;
};
}

class WebResourceManager::Impl
{
public:
    struct SnapshotStore : std::enable_shared_from_this<SnapshotStore>
    {
        QTemporaryDir directory;
        std::mutex mutex;
        QHash<QString, qint64> retryPaths;
        qint64 occupiedBytes = 0;
        bool retryScheduled = false;

        qint64 occupied()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return occupiedBytes;
        }

        void add(qint64 size)
        {
            std::lock_guard<std::mutex> lock(mutex);
            occupiedBytes += size;
        }

        void remove(const QString& path, qint64 size)
        {
            bool schedule = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (QFile::remove(path) || !QFileInfo::exists(path)) {
                    occupiedBytes -= size;
                }
                else {
                    retryPaths.insert(path, size);
                    if (!retryScheduled) {
                        retryScheduled = true;
                        schedule = true;
                    }
                }
            }
            if (schedule) {
                scheduleRetry();
            }
        }

        void retry(bool fromTimer = false)
        {
            bool schedule = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (fromTimer) {
                    retryScheduled = false;
                }
                for (auto it = retryPaths.begin(); it != retryPaths.end();) {
                    if (QFile::remove(it.key()) || !QFileInfo::exists(it.key())) {
                        occupiedBytes -= it.value();
                        it = retryPaths.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
                if (!retryPaths.isEmpty() && !retryScheduled) {
                    retryScheduled = true;
                    schedule = true;
                }
            }
            if (schedule) {
                scheduleRetry();
            }
        }

        void scheduleRetry()
        {
            auto* app = QCoreApplication::instance();
            if (!app) {
                std::lock_guard<std::mutex> lock(mutex);
                retryScheduled = false;
                return;
            }
            const std::weak_ptr<SnapshotStore> weak = shared_from_this();
            QTimer::singleShot(1000, app, [weak] {
                if (const auto store = weak.lock()) {
                    store->retry(true);
                }
            });
        }
    };

    struct Entry
    {
        QString path;
        QString mime;
        QString document;
        qint64 size = 0;
        QDateTime expiresAt;
        std::shared_ptr<SnapshotStore> snapshots;

        ~Entry()
        {
            if (snapshots) {
                snapshots->remove(path, size);
            }
        }
    };

    QUrl origin;
    QUrl documentOrigin;
    QString documentToken;
    std::shared_ptr<SnapshotStore> snapshots = std::make_shared<SnapshotStore>();
    QHash<QString, std::shared_ptr<Entry>> entries;
    QSet<QString> revoked;
    std::function<void(const QString&)> onRevoked;
    quint64 sessionDiagnosticId = 0;
    quint64 viewDiagnosticId = 0;
};

WebResourceManager::WebResourceManager(QUrl origin)
    : impl_(std::make_unique<Impl>())
{
    impl_->origin = std::move(origin);
}

WebResourceManager::~WebResourceManager()
{
    releaseAll();
}

void WebResourceManager::setDiagnosticContext(quint64 sessionId, quint64 viewId)
{
    impl_->sessionDiagnosticId = sessionId;
    impl_->viewDiagnosticId = viewId;
}

void WebResourceManager::setRevocationHandler(std::function<void(const QString&)> handler)
{
    impl_->onRevoked = std::move(handler);
}

void WebResourceManager::setContext(const QUrl& resourceOrigin, const QUrl& documentOrigin, const QString& documentToken)
{
    impl_->origin = resourceOrigin;
    impl_->documentOrigin = documentOrigin.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    setDocumentToken(documentToken);
}

void WebResourceManager::setDocumentToken(const QString& documentToken)
{
    if (impl_->documentToken != documentToken) {
        releaseForDocument(impl_->documentToken);
    }
    impl_->documentToken = documentToken;
}

PublishedResource WebResourceManager::publishFile(const QString& path, const QString& mimeType, const QString& documentToken)
{
    impl_->snapshots->retry();
    const auto now = QDateTime::currentDateTimeUtc();
    for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
        if (it.value()->expiresAt <= now) {
            impl_->revoked.insert(it.key());
            it = impl_->entries.erase(it);
        }
        else {
            ++it;
        }
    }
    const QFileInfo info(path);
    if (!impl_->origin.isValid() || impl_->documentToken.isEmpty() || (!documentToken.isEmpty() && documentToken != impl_->documentToken)
        || !info.isFile() || !info.isReadable() || info.size() > kMaximumFileSize || impl_->entries.size() >= kMaximumResourceCount
        || impl_->snapshots->occupied() + info.size() > kMaximumPublishedBytes || !impl_->snapshots->directory.isValid()) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.publish_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return { };
    }

    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto snapshotPath = QDir(impl_->snapshots->directory.path()).filePath(token);
    QFile input(info.absoluteFilePath());
    QSaveFile output(snapshotPath);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        qCWarning(systemWebViewResource).noquote()
            << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return { };
    }
    QByteArray buffer(static_cast<int>(kCopyBufferSize), Qt::Uninitialized);
    qint64 copied = 0;
    while (!input.atEnd()) {
        const auto count = input.read(buffer.data(), buffer.size());
        if (count <= 0 || output.write(buffer.constData(), count) != count) {
            output.cancelWriting();
            qCWarning(systemWebViewResource).noquote()
                << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
            return { };
        }
        copied += count;
        if (copied > kMaximumFileSize) {
            output.cancelWriting();
            qCWarning(systemWebViewResource).noquote()
                << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
            return { };
        }
    }
    if (copied != info.size() || !output.commit()) {
        qCWarning(systemWebViewResource).noquote()
            << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return { };
    }

    auto entry = std::make_shared<Impl::Entry>();
    entry->path = snapshotPath;
    entry->mime = mimeType.isEmpty() ? QMimeDatabase().mimeTypeForFile(info).name() : mimeType;
    entry->document = impl_->documentToken;
    entry->size = copied;
    entry->expiresAt = now.addMSecs(kResourceLifetimeMs);
    entry->snapshots = impl_->snapshots;
    impl_->snapshots->add(copied);
    impl_->entries.insert(token, entry);
    impl_->revoked.remove(token);
    const auto url = impl_->origin.resolved(QUrl(QStringLiteral("resource/%1").arg(token)));
    return { token, url, entry->mime, copied };
}

ResourceResponse WebResourceManager::open(const ResourceRequest& request) const
{
    const auto origin = request.url.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    const auto expectedOrigin = impl_->origin.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    const auto sourceOrigin = request.sourceOrigin.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    if (!expectedOrigin.isValid() || origin != expectedOrigin || sourceOrigin != impl_->documentOrigin) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=403";
        return { 403 };
    }

    const auto token = request.url.path().section(QLatin1Char('/'), -1);
    const auto it = impl_->entries.constFind(token);
    if (it == impl_->entries.cend()) {
        const int status = impl_->revoked.contains(token) ? 410 : 404;
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=" << status;
        return { status };
    }
    const auto entry = it.value();
    if (entry->expiresAt <= QDateTime::currentDateTimeUtc()) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=410";
        return { 410 };
    }
    if (!entry->document.isEmpty() && entry->document != request.documentToken) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=403";
        return { 403 };
    }

    qint64 start = request.rangeStart < 0 ? 0 : request.rangeStart;
    qint64 end = request.rangeEnd < 0 ? entry->size - 1 : request.rangeEnd;
    const bool hasRange = !request.rangeHeader.isEmpty() || request.rangeStart >= 0;
    if (!request.rangeHeader.isEmpty()) {
        static const QRegularExpression rangePattern(QStringLiteral("^bytes=(\\d*)-(\\d*)$"));
        const auto match = rangePattern.match(request.rangeHeader.trimmed());
        bool startOk = false;
        bool endOk = false;
        const auto first = match.captured(1);
        const auto last = match.captured(2);
        if (!match.hasMatch() || (first.isEmpty() && last.isEmpty())) {
            start = -1;
        }
        else if (first.isEmpty()) {
            const auto suffix = last.toLongLong(&endOk);
            start = endOk && suffix > 0 ? qMax<qint64>(0, entry->size - suffix) : -1;
            end = entry->size - 1;
        }
        else {
            start = first.toLongLong(&startOk);
            end = last.isEmpty() ? entry->size - 1 : last.toLongLong(&endOk);
            if (!last.isEmpty() && !endOk) {
                end = -1;
            }
            if (!startOk) {
                start = -1;
            }
            if (end >= entry->size) {
                end = entry->size - 1;
            }
        }
    }
    if ((hasRange && (entry->size == 0 || start < 0 || start > end || start >= entry->size))
        || (!hasRange && entry->size > 0 && (start < 0 || start > end))) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=416";
        ResourceResponse response;
        response.status = 416;
        response.mimeType = entry->mime;
        response.totalSize = entry->size;
        response.acceptRanges = true;
        return response;
    }
    const qint64 length = entry->size == 0 ? 0 : end - start + 1;
    auto file = std::make_unique<RangeFile>(entry->path, length);
    if (!file->open(QIODevice::ReadOnly) || !file->seek(start)) {
        qCWarning(systemWebViewResource).noquote()
            << "event=resource.read_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        ResourceResponse response;
        response.status = 500;
        response.mimeType = entry->mime;
        response.totalSize = entry->size;
        return response;
    }
    ResourceResponse response;
    response.status = hasRange ? 206 : 200;
    response.mimeType = entry->mime;
    response.totalSize = entry->size;
    response.offset = start;
    response.length = length;
    response.lease = entry;
    response.body = std::move(file);
    response.acceptRanges = true;
    return response;
}

void WebResourceManager::release(const QString& token)
{
    const auto it = impl_->entries.find(token);
    if (it == impl_->entries.end()) {
        return;
    }
    impl_->revoked.insert(token);
    impl_->entries.erase(it);
    if (impl_->onRevoked) {
        impl_->onRevoked(token);
    }
}

void WebResourceManager::releaseForDocument(const QString& documentToken)
{
    if (documentToken.isEmpty()) {
        return;
    }
    for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
        if (it.value()->document == documentToken) {
            impl_->revoked.insert(it.key());
            it = impl_->entries.erase(it);
        }
        else {
            ++it;
        }
    }
}

void WebResourceManager::releaseAll()
{
    for (auto it = impl_->entries.cbegin(); it != impl_->entries.cend(); ++it) {
        impl_->revoked.insert(it.key());
    }
    impl_->entries.clear();
}
} // namespace webview
