#pragma once

#include "webview/WebViewTypes.h"

namespace webview
{
class DocumentLifetime
{
public:
    quint64 token() const { return token_; }

    void invalidate()
    {
        if (!closed_) {
            ++token_;
        }
    }

    void close()
    {
        if (!closed_) {
            closed_ = true;
            ++token_;
        }
    }

    bool isClosed() const { return closed_; }

    MessageError resultFor(quint64 token) const
    {
        if (closed_) {
            return MessageError::Closed;
        }
        return token == token_ ? MessageError::None : MessageError::NavigationChanged;
    }

private:
    quint64 token_ = 0;
    bool closed_ = false;
};
} // namespace webview
