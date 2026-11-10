#include "webview/PathSecurity.h"

namespace webview
{
// ext4 and f2fs report symbolic links through lstat and hard links through st_nlink, so the portable
// checks cover app-private storage. Shared storage is mounted through FUSE, which may report a link count
// of one for every file and cannot represent hard links; a mapping rooted there therefore loses hard-link
// rejection, while errors reported by the emulated lstat still fail closed.
bool hasExternalFileLink(const std::filesystem::path& path)
{
    return hasExternalFileLinkPortable(path);
}
} // namespace webview
