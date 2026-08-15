# System WebView

A Qt 5 host for operating-system WebViews. The current backend uses macOS `WKWebView`; the public session, page, and policy contracts are designed to map to WebView2 and WebKit2GTK without exposing native types.

Build with Homebrew Qt 5:

```sh
cmake --preset default-debug
cmake --build --preset default-debug
ctest --preset default-debug
./build/debug/samples/demo/system_webview_demo
```

The real WKWebView page integration test needs access to macOS WindowServer. In a headless or restricted sandbox, run the core and session suites there and run `webview_macos_view_tests` in a logged-in GUI session.

The library requires explicit profile ownership:

```cpp
webview::WebViewPolicyConfig config;
config.allowedAppHosts.insert(QStringLiteral("my-app"));
auto policy = webview::createDefaultWebViewPolicy(std::move(config));
auto session = webview::createWebViewSession({ }, std::move(policy));
auto view = session->createWebView(parent);
```

The demo retains one session for all tabs, trusts only `app://demo` for its native bridge, and maps the allowlisted `https://example.com/` popup to a `QTabWidget` tab. See [the architecture guide](docs/ARCHITECTURE.md) for lifecycle, bridge, cleanup, platform mappings, and migration semantics.
