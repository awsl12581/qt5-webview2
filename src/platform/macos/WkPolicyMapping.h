#pragma once

#include "webview/WebViewTypes.h"

namespace webview
{
enum class NativePermissionDecision { Prompt, Grant, Deny };
enum class NativeDownloadDecision { Cancel, Download };

NativePermissionDecision mapPermissionDecision(PermissionDecision decision);
NativeDownloadDecision mapDownloadDecision(DownloadDecision decision);
} // namespace webview
