#include "webview/PathSecurity.h"

#include <system_error>

namespace webview
{
bool hasExternalFileLinkPortable(const std::filesystem::path& path)
{
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError || !std::filesystem::exists(status) || std::filesystem::is_symlink(status)
        || !std::filesystem::is_regular_file(status)) {
        return true;
    }
    std::error_code linkCountError;
    const auto linkCount = std::filesystem::hard_link_count(path, linkCountError);
    return linkCountError || linkCount > 1;
}
} // namespace webview
