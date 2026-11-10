#include "webview/PathSecurity.h"

namespace webview
{
// APFS and HFS+ report symbolic links through lstat and hard links through st_nlink, so the portable
// checks already describe every link type the file system can express. macOS aliases are regular files
// carrying Finder metadata, not file system links, and do not redirect reads.
bool hasExternalFileLink(const std::filesystem::path& path)
{
    return hasExternalFileLinkPortable(path);
}
} // namespace webview
