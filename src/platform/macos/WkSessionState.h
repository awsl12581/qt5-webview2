#pragma once

#include <unordered_set>

namespace webview
{
class WkWebView;

struct WkSessionState {
    bool valid = true;
    std::unordered_set<WkWebView*> views;
};
} // namespace webview
