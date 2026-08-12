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

The demo maps webpage `window.open()` calls to `QTabWidget` tabs. Applications install `setNewWindowHandler` to choose their own popup policy.
