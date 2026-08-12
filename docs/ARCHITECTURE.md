# System WebView Architecture

`system_webview_demo -> webview_platform -> webview_core`

`webview_core` exposes only Qt value types and the `IWebView` interface. Platform code owns native web view handles and is the sole location that imports native platform frameworks.

The bridge payload is a versioned JSON object. A page sends a message through a document-start transport. Native code receives approved messages through `WebViewHostCallbacks`; `IWebView::sendMessage` delivers an object back as a `system-webview-message` DOM event.

When a page requests a popup through `window.open()` or `target="_blank"`, the session policy decides whether creation is allowed before `WebViewHostCallbacks::newWindow` receives ownership of a `WebViewPtr`. The host may insert it into a tab, place it in a separate native window, or reject it by leaving the callback unset.

The sample intentionally uses only the system WebView. It does not link Qt WebEngine or bundle Chromium.
