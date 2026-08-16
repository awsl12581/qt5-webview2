#pragma once

#include "webview/WebViewTypes.h"

#include <atomic>

namespace webview
{
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

    MessageError resultFor(quint64 token) const
    {
        if (closed_.load()) {
            return MessageError::Closed;
        }
        return token == token_.load() ? MessageError::None : MessageError::NavigationChanged;
    }

private:
    std::atomic<quint64> token_ { 0 };
    std::atomic_bool closed_ = false;
};
} // namespace webview
