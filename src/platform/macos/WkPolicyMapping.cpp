#include "platform/macos/WkPolicyMapping.h"

namespace webview
{
NativePermissionDecision mapPermissionDecision(PermissionDecision decision)
{
    switch (decision) {
    case PermissionDecision::Allow:
        return NativePermissionDecision::Grant;
    case PermissionDecision::Deny:
        return NativePermissionDecision::Deny;
    case PermissionDecision::Unsupported:
        return NativePermissionDecision::Deny;
    }
    return NativePermissionDecision::Deny;
}

NativeDownloadDecision mapDownloadDecision(DownloadDecision decision)
{
    return decision == DownloadDecision::Allow ? NativeDownloadDecision::Download
                                               : NativeDownloadDecision::Cancel;
}
} // namespace webview
