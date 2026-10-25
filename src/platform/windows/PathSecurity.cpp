#include "webview/PathSecurity.h"

#include <windows.h>

namespace webview
{
bool hasExternalFileLink(const std::filesystem::path& path)
{
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
    FILE_STANDARD_INFO information { };
    const auto success = GetFileInformationByHandleEx(handle, FileStandardInfo, &information, sizeof(information)) != FALSE;
    const auto attributes = GetFileAttributesW(path.c_str());
    CloseHandle(handle);
    return !success || attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
           || information.NumberOfLinks > 1;
}
} // namespace webview
