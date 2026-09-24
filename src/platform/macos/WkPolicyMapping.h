#pragma once

#include <system_webview/system_webview.h>

namespace webview
{
class WebViewPolicy;

enum class NativePermissionDecision
{
    Prompt,
    Grant,
    Deny
};
enum class NativeDownloadDecision
{
    Cancel,
    Download
};

NativePermissionDecision mapPermissionDecision(PermissionDecision decision);
NativePermissionDecision decideNativePermission(const WebViewPolicy& policy, const PermissionRequest& request);
NativeDownloadDecision mapDownloadDecision(DownloadDecision decision);
} // namespace webview
