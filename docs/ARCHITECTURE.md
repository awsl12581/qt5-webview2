# System WebView Architecture

`system_webview_demo -> webview_platform -> webview_core`

`webview_core` exposes only Qt value types and the `IWebView` interface. Platform code owns native web view handles and is the sole location that imports native platform frameworks.

The bridge payload is a JSON object. A page sends a message through a document-start injected transport. Native code receives the object through `setMessageHandler`; `postMessage` delivers an object back as a `system-webview-message` DOM event.

When a page requests a popup through `window.open()` or `target="_blank"`, `IWebView::setNewWindowHandler` receives ownership of a newly-created `WebViewPtr`. The library does not choose a UI policy: the host may insert it into a tab, place it in a separate native window, or decline the request by leaving the handler unset.

The sample intentionally uses only the system WebView. It does not link Qt WebEngine or bundle Chromium.
