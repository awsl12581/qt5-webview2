#pragma once

#include <filesystem>

namespace webview
{
// Rejects symlinks, hard-linked files, non-regular files, and paths whose status cannot be read.
bool hasExternalFileLinkPortable(const std::filesystem::path& path);

// Returns true when the path is unsafe to serve. Platforms with additional link semantics build on the portable check.
bool hasExternalFileLink(const std::filesystem::path& path);
}
