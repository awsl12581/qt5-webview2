#include "webview/PathSecurity.h"

namespace webview
{
bool hasExternalFileLink(const std::filesystem::path&)
{
    return false;
}
} // namespace webview
