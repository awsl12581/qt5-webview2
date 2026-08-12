---
mission: spec
status: approved
created: 2026-08-12
approved_at: 2026-08-12T21:37:20+08:00
---

## Goal

Replace the page-only System WebView API with a platform-neutral API that gives applications explicit control of browser profile state, page navigation and lifecycle, security policy, and tab or window ownership. The API must have one defined contract that can be implemented by macOS WKWebView, Microsoft WebView2, and WebKit2GTK without exposing their native profile types to application code.

## Scope

This mission changes the public C++ API, the macOS backend, tests, demo, and architecture documentation.

It introduces `IWebViewSession`, `IWebView`, and `WebViewPolicy` as first-class library concepts. `WebViewHost` is an application-side ownership pattern documented by the library; it is not a mandatory core interface because Qt tabs, native windows, and embedding hosts have different ownership models.

The public factory creates sessions, and a session creates views. The legacy standalone `createWebView(QWidget*)` factory and its page-level bridge configuration methods are removed. No compatibility wrapper, deprecated overload, or implicit default session remains.

This mission defines the shared contract and implements it on macOS. WebView2 and WebKit2GTK implementations are not required in this mission, but their required mappings and semantic limits are part of the API design and testable contract.

## Design

### Layer contracts

`IWebViewSession` owns all state shared by related pages: persistent profile identity, cookies, cache and website data, the browser process or environment, and profile lifetime. It exposes `createWebView(QWidget*)`, `clearCache()`, `clearCookies()`, and `clearWebsiteData()`. The session factory requires an explicit session mode:

```cpp
std::unique_ptr<IWebViewSession> createPersistentSession(const QString& profilePath);
std::unique_ptr<IWebViewSession> createEphemeralSession();
```

Persistent sessions with the same canonical profile path must share the logical profile only when they are created by the same session object. Separate session objects have independent library ownership even when a backend's native API cannot guarantee process isolation for identical storage paths. Ephemeral sessions retain no website data after the session is destroyed.

`IWebView` owns one native page and its Qt widget. It exposes loading, stopping, reload, HTML loading, close, and asynchronous message delivery. `close()` is idempotent. It first prevents new callbacks, stops navigation, invalidates outstanding script completions, unregisters bridge and native delegates, releases the native view, then makes the C++ object inert. Destruction performs the same cleanup if `close()` was not called.

`WebViewPolicy` owns security decisions. It is supplied when creating a session and may be shared by all views in that session. It decides navigation, popups, bridge access, permissions, downloads, external URL handling, and message validation. Application callbacks receive typed requests and return explicit decisions rather than booleans where a third outcome exists.

`WebViewHost` owns `WebViewPtr` instances in a tab, window, or embedded surface. Before destroying a host surface it calls `view->close()` and then releases the `WebViewPtr`. Hosts install a new-window callback and choose whether a permitted popup is placed in a tab, a window, or discarded.

### Navigation and lifecycle

The public lifecycle is:

```cpp
enum class LoadState { Started, Redirected, Committed, Finished, Failed };

struct LoadEvent {
    LoadState state;
    QUrl url;
    QString error;
    bool isMainFrame;
    quint64 navigationId;
};

enum class NavigationDecision { Allow, Cancel, OpenExternally };

struct NavigationRequest {
    QUrl url;
    bool isMainFrame;
    bool isUserInitiated;
    bool isRedirect;
};
```

`setLoadHandler` receives main-frame events in their native order. `Started` identifies a provisional main-frame navigation; `Redirected` is emitted for each recognized main-frame redirect; `Committed` identifies the document that gained a committed origin; `Finished` and `Failed` terminate the matching `navigationId`. A navigation can fail before commit. Stale events and JavaScript completions from a prior `navigationId` must not be reported as results for the current document.

`setNavigationHandler` is consulted before every main-frame navigation and every subframe navigation that the backend can intercept. The default policy permits only `https`, controlled `file`, and registered `app` URLs. It rejects `javascript`, unknown schemes, malformed URLs, and all unapproved schemes. An `OpenExternally` decision delegates to the session host callback and cancels in-view navigation; if no external handler is installed, it behaves as `Cancel`.

Popup requests, including `target="_blank"` and `window.open`, are represented by a typed new-window request and evaluated by policy before any child native view is created. The default decision is rejection. A permitted request reaches the host's `setNewWindowHandler`; a missing handler rejects the popup.

### Bridge and script safety

Bridge availability is granted per committed main-frame origin. By default only `app` URLs are trusted; a policy may add explicit HTTPS origins. Subframes never receive bridge access. External pages receive no bridge.

The backend may install a minimal document-start transport when required by its native API, but native code must reject every bridge message unless the sender is the current main frame, its committed origin is trusted, and its document generation equals the active navigation. A navigation invalidates bridge authority before the next document can use it.

Bridge messages use a versioned JSON envelope with a fixed maximum serialized size, an allowed message-type set, and per-type schema validation. Native dispatch uses command allowlists and never invokes arbitrary method names. Native-to-page payloads are JSON serialized by the backend, never interpolated as JavaScript source. Pending JavaScript evaluation callbacks are cancelled or completed with a typed cancellation result on close or navigation invalidation.

Permissions for camera, microphone, location, notifications, clipboard, file pickers, and downloads are denied by default. Policy must explicitly authorize them for a trusted origin. Backend capabilities that cannot implement a permission category report a typed unsupported result rather than silently granting it.

### Backend mappings and portability rules

The API uses only the intersection of the three platform backends. Backend-specific features may be added later behind explicit capability discovery, not by changing common semantics.

| API concept | macOS | WebView2 | WebKit2GTK |
| --- | --- | --- | --- |
| Session/profile | `WKWebsiteDataStore` plus `WKProcessPool` | `CoreWebView2Environment` plus profile `userDataFolder` | `WebKitWebsiteDataManager` plus `WebKitWebContext` |
| Ephemeral session | `nonPersistentDataStore` | InPrivate profile when available; otherwise report unsupported | Ephemeral `WebKitWebsiteDataManager`/context configuration |
| Navigation lifecycle | `WKNavigationDelegate` | `NavigationStarting`, `SourceChanged`, `ContentLoading`, `NavigationCompleted` | `decide-policy`, `load-changed`, load failure signals |
| Popup policy | `WKUIDelegate` create-view callback | `NewWindowRequested` | `create` signal |
| Bridge validation | `WKScriptMessageHandler` frame/origin metadata | `WebMessageReceived` plus current source/frame policy | Script message handler plus frame URI/security origin |

The macOS session creates every `WKWebViewConfiguration` itself. Views in one session share its `WKWebsiteDataStore` and `WKProcessPool`, while each view has a separate `WKUserContentController` and delegates. A popup inherits the requesting session only after policy permits the request. The child must not inherit a bridge handler without a new origin authorization decision.

Architecture recall was unavailable: `lite-arch-recall` is not installed and the repository has no `docs/adr/` records.

## Compatibility

This is an intentional source-breaking major API change. Remove `createWebView(QWidget*)`, `IWebView::setMessageHandler`, `IWebView::setNewWindowHandler`, and unrestricted `IWebView::postMessage`. Their replacements belong to session policy and typed host callbacks. Do not retain forwarding overloads, deprecation shims, aliases, or implicit default-profile behavior.

`load(QUrl)` remains as the page navigation command, but its outcome is observable only through the lifecycle API and is subject to `WebViewPolicy`. `setHtml` is retained only if its supplied base URL passes policy and its generated document receives the same origin-based bridge rules; otherwise it must be removed in the implementation design.

## Non-goals

This mission does not implement WebView2 or WebKit2GTK backends, create a browser UI, provide a universal download manager, or define application-specific bridge commands. It also does not promise profile-level process isolation beyond what a platform backend supports.

## Security

No navigation, popup, bridge message, permission request, download, or external URL opens without a policy decision. Policy callbacks run on the owning UI thread and must not be retained beyond session destruction. Callback invocation is suppressed after a view closes. All untrusted web content is treated as unable to invoke privileged native functionality.

## Acceptance Criteria

- The public headers expose explicit session creation, session-scoped view creation, policy decisions, lifecycle events, close semantics, and typed popup handling; they contain no macOS, WebView2, or WebKitGTK types.
- The legacy standalone factory and direct page bridge/popup setter APIs are absent from public headers, documentation, demo, and tests.
- A macOS persistent session creates views that share `WKWebsiteDataStore` and `WKProcessPool`; an ephemeral session uses `nonPersistentDataStore` and leaves no website data after destruction.
- macOS emits ordered main-frame lifecycle events, supports `stop` and `reload`, identifies failures, and never reports stale navigation or script results as current-page results.
- Navigation policy blocks disallowed schemes before navigation; allowed external handling and popup handling obey explicit policy and host decisions; missing callbacks reject requests.
- The bridge accepts messages only from the current trusted main frame after commit, rejects iframe and untrusted-origin messages, enforces size/version/type/schema checks, and JSON serializes every outbound payload.
- Closing a tab invokes idempotent cleanup in the prescribed order and prevents all subsequent bridge, lifecycle, popup, and JavaScript completion callbacks.
- Unit tests cover policy defaults, message envelope validation, navigation-generation invalidation, and close idempotence. macOS integration tests cover session sharing, ephemeral storage, lifecycle mapping, navigation rejection, popup rejection, and trusted-origin bridge access.
- The demo owns views through a host object, creates an explicit session and policy, and closes a view before its tab widget is destroyed.
- Documentation states the platform mappings, semantic limits, removal of legacy APIs, and the expected migration from page-created views to session-created views.
