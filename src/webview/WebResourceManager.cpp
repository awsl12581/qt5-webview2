#include "internal/Diagnostics.h"
#include "internal/HttpStatus.h"
#include <system_webview/system_webview.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace webview
{
namespace
{
constexpr std::int64_t kMaximumFileSize = 2LL * 1024 * 1024 * 1024;
constexpr std::int64_t kMaximumPublishedBytes = 8LL * 1024 * 1024 * 1024;
constexpr int kMaximumResourceCount = 128;
constexpr std::int64_t kCopyBufferSize = 1024 * 1024;
constexpr std::int64_t kResourceLifetimeMs = 30LL * 60 * 1000;

class RangeFile final : public QFile
{
public:
    RangeFile(const std::string& path, std::int64_t length)
        : QFile(QString::fromStdString(path))
        , remaining_(length)
    {
    }

protected:
    qint64 readData(char* data, qint64 maximum) override
    {
        if (remaining_ <= 0) {
            return 0;
        }
        const auto count = QFile::readData(data, std::min<qint64>(maximum, remaining_));
        if (count > 0) {
            remaining_ -= count;
        }
        return count;
    }

private:
    std::int64_t remaining_;
};
}

class WebResourceManager::Impl
{
public:
    struct SnapshotStore : std::enable_shared_from_this<SnapshotStore>
    {
        QTemporaryDir directory;
        std::mutex mutex;
        std::unordered_map<std::string, std::int64_t> retryPaths;
        std::int64_t occupiedBytes = 0;
        bool retryScheduled = false;

        std::int64_t occupied()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return occupiedBytes;
        }

        void add(std::int64_t size)
        {
            std::lock_guard<std::mutex> lock(mutex);
            occupiedBytes += size;
        }

        void remove(const std::string& path, std::int64_t size)
        {
            bool schedule = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (QFile::remove(QString::fromStdString(path)) || !QFileInfo::exists(QString::fromStdString(path))) {
                    occupiedBytes -= size;
                }
                else {
                    retryPaths.insert_or_assign(path, size);
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
                    if (QFile::remove(QString::fromStdString(it->first)) || !QFileInfo::exists(QString::fromStdString(it->first))) {
                        occupiedBytes -= it->second;
                        it = retryPaths.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
                if (!retryPaths.empty() && !retryScheduled) {
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
        std::string path;
        std::string mime;
        std::string document;
        std::int64_t size = 0;
        std::chrono::system_clock::time_point expiresAt;
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
    std::string documentToken;
    std::shared_ptr<SnapshotStore> snapshots = std::make_shared<SnapshotStore>();
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries;
    std::unordered_set<std::string> revoked;
    std::function<void(const std::string&)> onRevoked;
    std::uint64_t sessionDiagnosticId = 0;
    std::uint64_t viewDiagnosticId = 0;
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

void WebResourceManager::setDiagnosticContext(std::uint64_t sessionId, std::uint64_t viewId)
{
    impl_->sessionDiagnosticId = sessionId;
    impl_->viewDiagnosticId = viewId;
}

void WebResourceManager::setRevocationHandler(std::function<void(const std::string&)> handler)
{
    impl_->onRevoked = std::move(handler);
}

void WebResourceManager::setContext(const QUrl& resourceOrigin, const QUrl& documentOrigin, const std::string& documentToken)
{
    impl_->origin = resourceOrigin;
    impl_->documentOrigin = documentOrigin.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
    setDocumentToken(documentToken);
}

void WebResourceManager::setDocumentToken(const std::string& documentToken)
{
    if (impl_->documentToken != documentToken) {
        releaseForDocument(impl_->documentToken);
    }
    impl_->documentToken = documentToken;
}

PublishedResource WebResourceManager::publishFile(const std::string& path, const std::string& mimeType, const std::string& documentToken)
{
    impl_->snapshots->retry();
    const auto now = std::chrono::system_clock::now();
    for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
        if (it->second->expiresAt <= now) {
            impl_->revoked.insert(it->first);
            it = impl_->entries.erase(it);
        }
        else {
            ++it;
        }
    }
    const QFileInfo info(QString::fromStdString(path));
    if (!impl_->origin.isValid() || impl_->documentToken.empty() || (!documentToken.empty() && documentToken != impl_->documentToken)
        || !info.isFile() || !info.isReadable() || info.size() > kMaximumFileSize || impl_->entries.size() >= kMaximumResourceCount
        || impl_->snapshots->occupied() + info.size() > kMaximumPublishedBytes || !impl_->snapshots->directory.isValid()) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.publish_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return { };
    }

    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto snapshotPath = impl_->snapshots->directory.filePath(QString::fromStdString(token));
    QFile input(info.absoluteFilePath());
    QSaveFile output(snapshotPath);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        qCWarning(systemWebViewResource).noquote()
            << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return { };
    }
    std::vector<char> buffer(static_cast<std::size_t>(kCopyBufferSize));
    std::int64_t copied = 0;
    while (!input.atEnd()) {
        const auto count = input.read(buffer.data(), buffer.size());
        if (count <= 0 || output.write(buffer.data(), count) != count) {
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
    entry->path = snapshotPath.toStdString();
    entry->mime = mimeType.empty() ? QMimeDatabase().mimeTypeForFile(info).name().toStdString() : mimeType;
    entry->document = impl_->documentToken;
    entry->size = copied;
    entry->expiresAt = now + std::chrono::milliseconds(kResourceLifetimeMs);
    entry->snapshots = impl_->snapshots;
    impl_->snapshots->add(copied);
    impl_->entries.insert_or_assign(token, entry);
    impl_->revoked.erase(token);
    const auto url = impl_->origin.resolved(QUrl(QString::fromStdString("resource/" + token)));
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
            << "origin=" << QString::fromStdString(diagnosticOrigin(request.url))
            << QStringLiteral("status=%1").arg(httpStatus::kForbidden);
        return { httpStatus::kForbidden };
    }

    const auto token = request.url.path().section(QLatin1Char('/'), -1).toStdString();
    const auto it = impl_->entries.find(token);
    if (it == impl_->entries.cend()) {
        const int status = impl_->revoked.contains(token) ? httpStatus::kGone : httpStatus::kNotFound;
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << QString::fromStdString(diagnosticOrigin(request.url)) << "status=" << status;
        return { status };
    }
    const auto entry = it->second;
    if (entry->expiresAt <= std::chrono::system_clock::now()) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << QString::fromStdString(diagnosticOrigin(request.url)) << QStringLiteral("status=%1").arg(httpStatus::kGone);
        return { httpStatus::kGone };
    }
    if (!entry->document.empty() && entry->document != request.documentToken) {
        qCDebug(systemWebViewResource).noquote()
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << QString::fromStdString(diagnosticOrigin(request.url))
            << QStringLiteral("status=%1").arg(httpStatus::kForbidden);
        return { httpStatus::kForbidden };
    }

    std::int64_t start = request.rangeStart < 0 ? 0 : request.rangeStart;
    std::int64_t end = request.rangeEnd < 0 ? entry->size - 1 : request.rangeEnd;
    const bool hasRange = !request.rangeHeader.empty() || request.rangeStart >= 0;
    if (!request.rangeHeader.empty()) {
        static const QRegularExpression rangePattern(QStringLiteral("^bytes=(\\d*)-(\\d*)$"));
        const auto match = rangePattern.match(QString::fromStdString(request.rangeHeader).trimmed());
        bool startOk = false;
        bool endOk = false;
        const auto first = match.captured(1);
        const auto last = match.captured(2);
        if (!match.hasMatch() || (first.isEmpty() && last.isEmpty())) {
            start = -1;
        }
        else if (first.isEmpty()) {
            const auto suffix = last.toLongLong(&endOk);
            start = endOk && suffix > 0 ? std::max<std::int64_t>(0, entry->size - suffix) : -1;
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
            << "origin=" << QString::fromStdString(diagnosticOrigin(request.url))
            << QStringLiteral("status=%1").arg(httpStatus::kRangeNotSatisfiable);
        ResourceResponse response;
        response.status = httpStatus::kRangeNotSatisfiable;
        response.mimeType = entry->mime;
        response.totalSize = entry->size;
        response.acceptRanges = true;
        return response;
    }
    const std::int64_t length = entry->size == 0 ? 0 : end - start + 1;
    auto file = std::make_unique<RangeFile>(entry->path, length);
    if (!file->open(QIODevice::ReadOnly) || !file->seek(start)) {
        qCWarning(systemWebViewResource).noquote()
            << "event=resource.read_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        ResourceResponse response;
        response.status = httpStatus::kInternalServerError;
        response.mimeType = entry->mime;
        response.totalSize = entry->size;
        return response;
    }
    ResourceResponse response;
    response.status = hasRange ? httpStatus::kPartialContent : httpStatus::kOk;
    response.mimeType = entry->mime;
    response.totalSize = entry->size;
    response.offset = start;
    response.length = length;
    response.lease = entry;
    response.body = std::move(file);
    response.acceptRanges = true;
    return response;
}

void WebResourceManager::release(const std::string& token)
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

void WebResourceManager::releaseForDocument(const std::string& documentToken)
{
    if (documentToken.empty()) {
        return;
    }
    for (auto it = impl_->entries.begin(); it != impl_->entries.end();) {
        if (it->second->document == documentToken) {
            impl_->revoked.insert(it->first);
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
        impl_->revoked.insert(it->first);
    }
    impl_->entries.clear();
}
} // namespace webview
