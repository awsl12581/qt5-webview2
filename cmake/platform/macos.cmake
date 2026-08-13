enable_language(OBJCXX)

find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
find_library(COCOA_FRAMEWORK Cocoa REQUIRED)

target_sources(system_webview PRIVATE
    src/webview/WebViewFactory.cpp
    src/platform/macos/WkPolicyMapping.cpp
    src/platform/macos/WkWebView.mm
    src/platform/macos/WkWebViewSession.mm
)
target_link_libraries(system_webview PRIVATE
    "${WEBKIT_FRAMEWORK}"
    "${COCOA_FRAMEWORK}"
)
