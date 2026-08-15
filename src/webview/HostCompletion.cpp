#include "webview/HostCompletion.h"

#include "webview/WebViewState.h"

#include <QFileInfo>

namespace webview
{
HostCompletionGuard::HostCompletionGuard(const std::shared_ptr<WebViewState>& state)
    : state_(state)
{
}

HostCompletionAccess HostCompletionGuard::claim()
{
    if (completed_.exchange(true)) {
        return { HostCompletionClaim::Duplicate, { } };
    }
    auto state = state_.lock();
    if (!state || state->lifetime.isClosed()) {
        return { HostCompletionClaim::OwnerUnavailable, { } };
    }
    return { HostCompletionClaim::Accepted, std::move(state) };
}

FileSelectionResult normalizeFileSelectionResult(
    const FileSelectionRequest& request, FileSelectionResult result)
{
    if (result.status != FileSelectionStatus::Selected || result.paths.isEmpty()) {
        if (result.status == FileSelectionStatus::Selected) {
            result.status = FileSelectionStatus::Cancelled;
        }
        result.paths.clear();
        return result;
    }
    if (!request.allowsMultiple && result.paths.size() != 1) {
        return { FileSelectionStatus::InvalidResult, { },
            QStringLiteral("The file request allows only one selection.") };
    }
    for (const auto& path : result.paths) {
        const QFileInfo entry(path);
        if (path.isEmpty() || !entry.exists()) {
            return { FileSelectionStatus::InvalidResult, { },
                QStringLiteral("A selected path does not exist.") };
        }
        if (entry.isDir() && !request.allowsDirectories) {
            return { FileSelectionStatus::InvalidResult, { },
                QStringLiteral("The file request does not allow directories.") };
        }
    }
    return result;
}
} // namespace webview
