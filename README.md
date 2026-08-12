# System WebView

A Qt5 host for the operating system's WebView. The initial macOS backend uses `WKWebView`, with no Qt WebEngine or bundled Chromium.

Build with Homebrew Qt5:

```sh
cmake --preset default-debug
cmake --build --preset default-debug
ctest --preset default-debug
./build/debug/system_webview_demo
```

The `system_webview` static library is the product. Its public boundary is `src/webview/IWebView.h`; platform-native views are isolated below `src/platform`. Runnable examples live below `samples/`, starting with `samples/demo`.

The demo creates every page from an explicit session and maps permitted webpage popup requests to `QTabWidget` tabs through `WebViewHostCallbacks`.
