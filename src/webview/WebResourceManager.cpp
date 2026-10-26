#include "internal/Diagnostics.h"
#include "internal/FileResource.h"
#include "internal/HttpStatus.h"
#include "internal/MimeType.h"
#include <system_webview/system_webview.h>

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

}

class WebResourceManager::Impl
{
public:
    struct SnapshotStore : std::enable_shared_from_this<SnapshotStore>
    {
        std::filesystem::path directory;
        std::mutex mutex;
        std::unordered_map<std::string, std::int64_t> retryPaths;
        std::int64_t occupiedBytes = 0;
        bool retryScheduled = false;

        SnapshotStore()
        {
            std::error_code error;
            const auto root = std::filesystem::temp_directory_path(error);
            if (!error) {
                for (int attempt = 0; attempt != 8 && directory.empty(); ++attempt) {
                    const auto candidate = root / ("system-webview-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
                    if (std::filesystem::create_directory(candidate, error)) {
                        directory = candidate;
                    }
                    error.clear();
                }
            }
        }

        ~SnapshotStore()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }

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
                std::error_code error;
                if (std::filesystem::remove(pathFromUtf8(path), error) || !std::filesystem::exists(pathFromUtf8(path))) {
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
                    std::error_code error;
                    if (std::filesystem::remove(pathFromUtf8(it->first), error) || !std::filesystem::exists(pathFromUtf8(it->first))) {
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

    Url origin;
    Url documentOrigin;
    std::string documentToken;
    std::shared_ptr<SnapshotStore> snapshots = std::make_shared<SnapshotStore>();
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries;
    std::unordered_set<std::string> revoked;
    std::function<void(const std::string&)> onRevoked;
    std::uint64_t sessionDiagnosticId = 0;
    std::uint64_t viewDiagnosticId = 0;
};

WebResourceManager::WebResourceManager(Url origin)
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

void WebResourceManager::setContext(const Url& resourceOrigin, const Url& documentOrigin, const std::string& documentToken)
{
    impl_->origin = resourceOrigin;
    impl_->documentOrigin = documentOrigin.origin();
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
    std::error_code fileError;
    const auto sourcePath = pathFromUtf8(path);
    const bool sourceIsFile = std::filesystem::is_regular_file(sourcePath, fileError);
    const auto sourceSize = sourceIsFile ? std::filesystem::file_size(sourcePath, fileError) : 0;
    if (!impl_->origin.isValid() || impl_->documentToken.empty() || (!documentToken.empty() && documentToken != impl_->documentToken)
        || !sourceIsFile || fileError || sourceSize > kMaximumFileSize || impl_->entries.size() >= kMaximumResourceCount
        || impl_->snapshots->occupied() + static_cast<std::int64_t>(sourceSize) > kMaximumPublishedBytes
        || impl_->snapshots->directory.empty()) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.resource")
            << "event=resource.publish_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return {};
    }

    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto snapshotPath = impl_->snapshots->directory / token;
    const auto temporaryPath = pathFromUtf8(pathToUtf8(snapshotPath) + ".tmp");
    std::ifstream input(sourcePath, std::ios::binary);
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.resource")
            << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(kCopyBufferSize));
    std::int64_t copied = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && !output.write(buffer.data(), count)) {
            output.close();
            std::error_code error;
            std::filesystem::remove(temporaryPath, error);
            diagnosticLog(DiagnosticLevel::Warning, "system_webview.resource")
                << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
            return {};
        }
        copied += count;
        if (copied > kMaximumFileSize) {
            output.close();
            std::error_code error;
            std::filesystem::remove(temporaryPath, error);
            diagnosticLog(DiagnosticLevel::Warning, "system_webview.resource")
                << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
            return {};
        }
    }
    output.flush();
    const bool outputGood = output.good();
    output.close();
    std::error_code commitError;
    std::filesystem::rename(temporaryPath, snapshotPath, commitError);
    if (!outputGood || copied != static_cast<std::int64_t>(sourceSize) || commitError) {
        std::error_code error;
        std::filesystem::remove(temporaryPath, error);
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.resource")
            << "event=resource.snapshot_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        return {};
    }

    auto entry = std::make_shared<Impl::Entry>();
    entry->path = pathToUtf8(snapshotPath);
    entry->mime = mimeType.empty() ? std::string(mime::fromPath(path)) : mimeType;
    entry->document = impl_->documentToken;
    entry->size = copied;
    entry->expiresAt = now + std::chrono::milliseconds(kResourceLifetimeMs);
    entry->snapshots = impl_->snapshots;
    impl_->snapshots->add(copied);
    impl_->entries.insert_or_assign(token, entry);
    impl_->revoked.erase(token);
    const auto url = impl_->origin.resolved("resource/" + token);
    return { token, url, entry->mime, copied };
}

ResourceResponse WebResourceManager::open(const ResourceRequest& request) const
{
    const auto origin = request.url.origin();
    const auto sourceOrigin = request.sourceOrigin.origin();
    if (!impl_->origin.isValid() || origin != impl_->origin.origin() || sourceOrigin != impl_->documentOrigin) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.resource")
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=" << httpStatus::kForbidden;
        return { httpStatus::kForbidden };
    }

    const auto resourcePath = request.url.path();
    const auto slash = resourcePath.find_last_of('/');
    const auto token = slash == std::string::npos ? resourcePath : resourcePath.substr(slash + 1);
    const auto it = impl_->entries.find(token);
    if (it == impl_->entries.cend()) {
        const int status = impl_->revoked.contains(token) ? httpStatus::kGone : httpStatus::kNotFound;
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.resource")
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=" << status;
        return { status };
    }
    const auto entry = it->second;
    if (entry->expiresAt <= std::chrono::system_clock::now()) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.resource")
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=" << httpStatus::kGone;
        return { httpStatus::kGone };
    }
    if (!entry->document.empty() && entry->document != request.documentToken) {
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.resource")
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=" << httpStatus::kForbidden;
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
        diagnosticLog(DiagnosticLevel::Debug, "system_webview.resource")
            << "event=resource.request_rejected" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId
            << "origin=" << diagnosticOrigin(request.url) << "status=" << httpStatus::kRangeNotSatisfiable;
        ResourceResponse response;
        response.status = httpStatus::kRangeNotSatisfiable;
        response.mimeType = entry->mime;
        response.totalSize = entry->size;
        response.acceptRanges = true;
        return response;
    }
    const std::int64_t length = entry->size == 0 ? 0 : end - start + 1;
    auto file = std::make_unique<FileResource>(entry->path, start + length);
    if (!file->seek(start)) {
        diagnosticLog(DiagnosticLevel::Warning, "system_webview.resource")
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
