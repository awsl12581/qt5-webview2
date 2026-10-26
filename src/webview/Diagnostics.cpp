#include "internal/Diagnostics.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace webview
{
namespace
{
std::mutex handlerMutex;
DiagnosticHandler handler;
}

DiagnosticStream::DiagnosticStream(DiagnosticLevel level, std::string_view category)
    : level_(level)
    , category_(category)
{
}

DiagnosticStream::DiagnosticStream(DiagnosticStream&& other) noexcept
    : level_(other.level_)
    , category_(std::move(other.category_))
    , stream_(std::move(other.stream_))
    , active_(std::exchange(other.active_, false))
{
}

DiagnosticStream::~DiagnosticStream()
{
    if (!active_) {
        return;
    }
    DiagnosticHandler callback;
    {
        std::lock_guard<std::mutex> lock(handlerMutex);
        callback = handler;
    }
    if (callback) {
        try {
            callback({ level_, std::move(category_), stream_.str() });
        }
        catch (...) {
        }
    }
}

DiagnosticStream& DiagnosticStream::operator=(DiagnosticStream&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    level_ = other.level_;
    category_ = std::move(other.category_);
    stream_ = std::move(other.stream_);
    active_ = std::exchange(other.active_, false);
    return *this;
}

DiagnosticStream diagnosticLog(DiagnosticLevel level, std::string_view category)
{
    return DiagnosticStream(level, category);
}

void setDiagnosticHandler(DiagnosticHandler newHandler)
{
    std::lock_guard<std::mutex> lock(handlerMutex);
    handler = std::move(newHandler);
}

std::uint64_t nextDiagnosticId()
{
    static std::atomic<std::uint64_t> nextId { 1 };
    return nextId.fetch_add(1, std::memory_order_relaxed);
}

std::string diagnosticOrigin(const Url& url)
{
    if (!url.isValid() || url.scheme().empty()) {
        return "invalid";
    }
    return url.origin().toString();
}
} // namespace webview
