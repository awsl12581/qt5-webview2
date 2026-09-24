#pragma once

#include <QtGlobal>

#include <atomic>

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
    quint64 token() const { return token_.load(); }

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

    DocumentError resultFor(quint64 token) const
    {
        if (closed_.load()) {
            return DocumentError::Closed;
        }
        return token == token_.load() ? DocumentError::None : DocumentError::NavigationChanged;
    }

private:
    std::atomic<quint64> token_ { 0 };
    std::atomic_bool closed_ = false;
};
} // namespace webview
