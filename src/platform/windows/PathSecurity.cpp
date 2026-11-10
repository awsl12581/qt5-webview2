#include "webview/PathSecurity.h"

#include <windows.h>

namespace webview
{
bool hasExternalFileLink(const std::filesystem::path& path)
{
    if (hasExternalFileLinkPortable(path)) {
        return true;
    }
    // The portable check only reports name-surrogate reparse points as symlinks. Opening with
    // FILE_FLAG_OPEN_REPARSE_POINT and inspecting the attribute also rejects cloud placeholders,
    // deduplicated files, and other reparse tags, and fails closed when the path cannot be opened.
    const auto handle = CreateFileW(
        path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return true;
    }
    const auto attributes = GetFileAttributesW(path.c_str());
    CloseHandle(handle);
    return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
} // namespace webview
