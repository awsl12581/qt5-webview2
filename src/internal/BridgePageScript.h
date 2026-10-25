#pragma once

#include <string>
#include <string_view>

namespace webview
{
std::string bridgePageScript(
    std::string_view documentTokenExpression,
    std::string_view nativePostExpression,
    std::string_view nativeReceiveRegistration,
    std::string_view prelude = {});
}
