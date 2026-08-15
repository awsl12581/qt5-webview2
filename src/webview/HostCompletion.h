#pragma once

#include "webview/WebViewTypes.h"

#include <atomic>
#include <memory>

namespace webview
{
class WebViewState;

enum class HostCompletionClaim { Accepted, Duplicate, OwnerUnavailable };

struct HostCompletionAccess {
    HostCompletionClaim claim = HostCompletionClaim::OwnerUnavailable;
    std::shared_ptr<WebViewState> state;
};

class HostCompletionGuard final
{
public:
    explicit HostCompletionGuard(const std::shared_ptr<WebViewState>& state);
    HostCompletionAccess claim();

private:
    std::atomic_bool completed_ = false;
    std::weak_ptr<WebViewState> state_;
};

FileSelectionResult normalizeFileSelectionResult(
    const FileSelectionRequest& request, FileSelectionResult result);
} // namespace webview
