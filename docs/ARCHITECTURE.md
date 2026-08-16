# System WebView Architecture

`system_webview_demo -> webview platform backend -> portable webview contract`

The public headers under `src/webview` expose Qt value types and C++ interfaces only. Native WebKit, WebView2, and WebKitGTK types stay below `src/platform`.

## Ownership model

The library separates four responsibilities:

1. `IWebViewSession` owns profile state shared by related pages: website data, cookies, cache, and the native browser process or environment. A session creates every `IWebView`.
2. `IWebView` owns one native page, its Qt widget, navigation lifecycle, and document-scoped message delivery.
3. `WebViewPolicy` decides navigation, popups, bridge access, permissions, downloads, and message schemas. Defaults fail closed except for ordinary HTTPS navigation.
4. The application host owns `WebViewPtr` instances in tabs or windows. It calls `close()` before releasing a view or destroying its surface.

Destroying a session closes every root or popup view it created, even when the host still retains the `WebViewPtr`. Those objects remain safe to destroy or query with `isClosed()`, but are otherwise inert and no longer retain the session policy.

There is no standalone view factory or implicit default session. Applications choose a persistent or ephemeral session explicitly:

```cpp
webview::WebViewSessionOptions options;
options.mode = webview::SessionMode::Persistent;
options.profilePath = profilePath;
auto session = webview::createWebViewSession(std::move(options), policy);
auto view = session->createWebView(parent);
// The host inserts widget() into its final layout before attaching the native page.
view->attachNativeView();
```

`SessionMode::Ephemeral` selects non-persistent native website storage. On macOS, `profilePath` is a logical application profile identifier: public `WKWebsiteDataStore` APIs do not support assigning an arbitrary persistent storage directory, so persistent sessions use `defaultDataStore`. Applications must not treat the path as a filesystem placement guarantee.

## Navigation and lifecycle

`LoadEvent` reports `Started`, `Redirected`, `Committed`, `Finished`, and `Failed`. Events for one native navigation retain one `navigationId`, including overlapping navigation failures. A rejected policy decision occurs before provisional navigation and therefore does not emit a synthetic load sequence.

Hosts receive lifecycle, external URL, popup, and bridge events through one `WebViewHostCallbacks` object. `stop()` and `reload()` forward to the native page. `close()` is idempotent and performs this order:

1. Mark the page closed and invalidate its document token.
2. Clear host callbacks and popup creation.
3. Stop loading and detach native delegates and message handlers.
4. Remove and release the native view.

Queued native callbacks see the closed state or a changed document token and cannot be reported as results for a newer page.

The host owns native attachment timing. It adds `view->widget()` to the final tab or window layout, activates the layout, and then calls `attachNativeView()`. `detachNativeView()` is called before the host surface is moved or destroyed. Backends do not infer attachment from visibility, parent widgets, default size, or event-loop timing.

## Policy and bridge

The default policy permits HTTPS navigation. `app://` hosts and `file://` roots require explicit allowlisting. Malformed URLs, `javascript:`, unknown schemes, popups, downloads, media capture, and other permissions are rejected by default. An `OpenExternally` decision cancels in-view navigation and invokes the host only when an external handler exists.

`IWebViewSession::capabilitySupport` reports whether the current backend can represent a capability. On macOS, camera and microphone require macOS 12, browser-default downloads require macOS 11.3, and file selection is host-owned. Location, notifications, clipboard, and explicit download destinations currently report `Unsupported` rather than being silently granted.

File selection and download destinations are host decisions. The backend requests a result through `selectFiles` or `resolveDownload`; it does not create a system dialog or choose a local directory. Popup ownership is also the result: the backend transfers a child `WebViewPtr` by value, and the host accepts by retaining it in a tab or window before the callback returns. Local `app://` content must be declared through `WebViewSessionOptions::resourceMappings`; macOS serves validated mappings through a private `WKURLSchemeHandler`.

Bridge authority belongs to the current committed main-frame document. A trusted `app://` host or exact HTTPS origin must be configured. Untrusted pages, pre-commit documents, and an origin different from the committed page are rejected.

On macOS, trusted pages send through `window.systemWebView.postMessage(message)`. The document-start transport attaches an unguessable token that native code rotates before each allowed main-frame navigation. The raw `window.webkit.messageHandlers.systemWebView` object is a backend detail: messages without the current token, including iframe calls and queued messages from an earlier same-origin document, are rejected.

On Windows, the WebView2 SDK interface used by this backend exposes the message source URL but no direct main-frame flag on `ICoreWebView2WebMessageReceivedEventArgs`. The backend therefore does not describe its origin comparison as frame validation. It injects the current token into the top-level document, then requires that token together with the committed origin, message size, protocol version, type, and payload schema. Cross-origin frames fail the origin check; frames without the top-level token and stale documents fail the token check. A hostile same-origin frame that can obtain the top-level token is outside the guarantees of this SDK path.

Messages use this envelope:

```json
{"version":1,"type":"command-name","payload":{}}
```

Policy sets the maximum serialized size, allowed message types, and required payload fields for each type. Dispatch is by an allowed type, never by an arbitrary native method name. Native-to-page messages are serialized with `QJsonDocument`; source text is not assembled from unescaped payload strings. A navigation or close invalidates pending JavaScript completion callbacks.

## Backend mapping

| Common concept | macOS WKWebView | Microsoft WebView2 | WebKit2GTK |
| --- | --- | --- | --- |
| Session/profile | `WKWebsiteDataStore` and configuration identity | `CoreWebView2Environment` and profile/user data folder | `WebKitWebsiteDataManager` and `WebKitWebContext` |
| Ephemeral session | `nonPersistentDataStore` | InPrivate profile when supported | Ephemeral website data manager/context |
| Lifecycle | `WKNavigationDelegate` | Navigation starting/source/content/completed events | policy decision, load-changed, and failure signals |
| Popup | `WKUIDelegate` | `NewWindowRequested` | `create` signal |
| Bridge | main-frame `WKScriptMessageHandler` plus origin checks | web-message source, top-level document token, and schema checks; no direct frame flag | script-message frame URI/origin checks |

The macOS WKWebView and Windows WebView2 backends are implemented. WebKit2GTK remains a design mapping only. Backend selection is compile-time through CMake platform branches and preprocessor conditions; there is no runtime plugin loader or backend registry.

On Windows, vcpkg manifest mode provides the ARM64 SDK, import libraries, `WebView2Loader.dll`, Qt, and app-local dependency deployment during the CMake build. The Microsoft Edge WebView2 Evergreen Runtime is an operating-system/application prerequisite and is not installed by this library, the SDK package, or the Loader DLL.

Views in one macOS session share its website data store and assigned `WKProcessPool`; each view receives a separate `WKUserContentController` and delegate set. `WKProcessPool` is deprecated on macOS 12 and later because multiple instances no longer affect isolation. It remains assigned for older systems and configuration identity, not as a modern process-isolation guarantee.

## Migration

The session API is a source-breaking replacement. Remove calls to old session factory names, former standalone view factories, page-level bridge or popup setters, and concrete `WkWebView` casts. There are no forwarding overloads or deprecation shims. Migration consists of constructing `WebViewSessionOptions`, retaining one session per login/profile boundary, creating views from it, inserting each widget into its final host layout, attaching the view, installing policy at session creation, and assigning `WebViewHostCallbacks` to each view.

`setHtml` remains available only when its base URL passes navigation policy. It follows the same committed-origin bridge checks as network navigation.
