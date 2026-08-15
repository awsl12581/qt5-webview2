# The WebView2 backend belongs here. Keep the core library backend-free until
# that implementation is added, so Qt Widgets tooling can still be validated.
target_compile_definitions(system_webview PRIVATE SYSTEM_WEBVIEW_PLATFORM_WINDOWS=1)
