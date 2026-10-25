#pragma once

#include <filesystem>

namespace webview
{
bool hasExternalFileLink(const std::filesystem::path& path);
}
