# System WebView Architecture

`system_webview_demo -> webview platform backend -> portable webview contract`

The public headers under `src/webview` expose Qt value types and C++ interfaces only. Native WebKit, WebView2, and WebKitGTK types stay below `src/platform`.

## Ownership model

The library separates four responsibilities:

1. `IWebViewSession` owns profile state shared by related pages: website data, cookies, cache, and the native browser process or environment. A session creates every `IWebView`.
2. `IWebView` owns one native page, its Qt widget, navigation lifecycle, and document-scoped message delivery.
3. `WebViewPolicy` decides navigation, popups, bridge access, permissions, downloads, and message schemas. Defaults fail closed except for ordinary HTTPS navigation.
4. The application host owns `WebViewPtr` instances in tabs or windows. It calls `close()` before releasing a view or destroying its surface.

There is no standalone view factory or implicit default session. Applications choose a persistent or ephemeral session explicitly:

```cpp
auto session = webview::createPersistentSession(profilePath, policy);
auto view = session->createWebView(parent);
```

`createEphemeralSession` selects non-persistent native website storage. On macOS, `profilePath` is a logical application profile identifier: public `WKWebsiteDataStore` APIs do not support assigning an arbitrary persistent storage directory, so persistent sessions use `defaultDataStore`. Applications must not treat the path as a filesystem placement guarantee.

## Navigation and lifecycle

`LoadEvent` reports `Started`, `Redirected`, `Committed`, `Finished`, and `Failed`. Events for one native navigation retain one `navigationId`, including overlapping navigation failures. A rejected policy decision occurs before provisional navigation and therefore does not emit a synthetic load sequence.

Hosts receive lifecycle, external URL, popup, and bridge events through one `WebViewHostCallbacks` object. `stop()` and `reload()` forward to the native page. `close()` is idempotent and performs this order:

1. Mark the page closed and invalidate its document token.
2. Clear host callbacks and popup creation.
3. Stop loading and detach native delegates and message handlers.
4. Remove and release the native view.

Queued native callbacks see the closed state or a changed document token and cannot be reported as results for a newer page.

## Policy and bridge

The default policy permits HTTPS navigation. `app://` hosts and `file://` roots require explicit allowlisting. Malformed URLs, `javascript:`, unknown schemes, popups, downloads, media capture, and other permissions are rejected by default. An `OpenExternally` decision cancels in-view navigation and invokes the host only when an external handler exists.

Bridge authority belongs to the current committed main-frame origin. A trusted `app://` host or exact HTTPS origin must be configured. Subframes, untrusted pages, pre-commit documents, and an origin different from the committed page are rejected.

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
| Bridge | main-frame `WKScriptMessageHandler` plus origin checks | web-message source/frame checks | script-message frame URI/origin checks |

Only the macOS backend is implemented. The common API deliberately uses semantics that the other two designs can map, but this repository does not claim WebView2 or WebKit2GTK production support.

Views in one macOS session share its website data store and assigned `WKProcessPool`; each view receives a separate `WKUserContentController` and delegate set. `WKProcessPool` is deprecated on macOS 12 and later because multiple instances no longer affect isolation. It remains assigned for older systems and configuration identity, not as a modern process-isolation guarantee.

## Migration

The session API is a source-breaking replacement. Remove calls to the former standalone view factory and page-level bridge or popup setters. There are no forwarding overloads or deprecation shims. Migration consists of retaining one session per login/profile boundary, creating views from it, installing policy at session creation, and assigning `WebViewHostCallbacks` to each view.

`setHtml` remains available only when its base URL passes navigation policy. It follows the same committed-origin bridge checks as network navigation.
