#pragma once

#include <atomic>
#include <cstdint>

namespace webview
{
enum class DocumentError
{
    None,
    Closed,
    NavigationChanged
};

class DocumentLifetime
{
public:
    std::uint64_t token() const { return token_.load(); }

    void invalidate()
    {
        if (!closed_.load()) {
            token_.fetch_add(1);
        }
    }

    void close()
    {
        if (!closed_.exchange(true)) {
            token_.fetch_add(1);
        }
    }

    bool isClosed() const { return closed_.load(); }

    DocumentError resultFor(std::uint64_t token) const
    {
        if (closed_.load()) {
            return DocumentError::Closed;
        }
        return token == token_.load() ? DocumentError::None : DocumentError::NavigationChanged;
    }

private:
    std::atomic<std::uint64_t> token_ { 0 };
    std::atomic_bool closed_ = false;
};
} // namespace webview
