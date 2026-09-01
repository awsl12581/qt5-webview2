# macOS and Windows Platform Version and Behavior Matrix

> Snapshot date: 2026-08-16
>
> Source revision: `7259eee` (`test(windows): record non-UI regression evidence`)
>
> Scope: the public `IWebViewSession` / `IWebView` contract implemented by the macOS WKWebView and Windows WebView2 backends. This document distinguishes implemented code from evidence obtained from a real desktop Runtime.

## Read This First

The two backends share the same public C++ API, but they do not have identical runtime guarantees.

- macOS is the established backend. Its session and page integration tests are intended to run in a logged-in macOS WindowServer session.
- Windows is compiled for ARM64 and uses WebView2. Its production code includes the controller, profile, navigation, bridge, resource, popup, and download paths, but GUI E2E was explicitly not run on 2026-08-16. Do not report static checks, builds, or contract tests as successful Windows page E2E.
- `vcpkg` installs the WebView2 SDK and Loader DLL used at build/deployment time. It does not install the Microsoft Edge WebView2 Evergreen Runtime required on the target device.

## Version Requirements

| Area | macOS WKWebView | Windows WebView2 | Resulting behavior |
| --- | --- | --- | --- |
| Base backend | macOS with WebKit/Cocoa available | Windows ARM64, MSVC, WebView2 SDK/Loader and Evergreen Runtime | Backend selection is compile-time through CMake. |
| Native-to-page bridge API | macOS 11.0+: `callAsyncJavaScript` | `PostWebMessageAsJson` from the WebView2 Runtime | macOS returns `Unsupported` for outbound bridge delivery below 11.0. Windows requires a functioning controller. |
| Download destination | macOS 11.3+: `WKDownloadDelegate` destination callback | WebView2 `ICoreWebView2_4` download-starting event and deferral | macOS reports `DownloadTarget` unsupported below 11.3. Windows exposes target selection in code but needs Runtime E2E verification. |
| Camera and microphone | macOS 12.0+ | WebView2 permission event; policy maps camera/microphone | macOS reports both unsupported below 12.0. Windows reports no capability support for these permissions even though the policy handler exists. |
| File selection | WK open-panel delegate | No supported host chooser interception in the SDK used by this project | macOS supports host-owned selection; Windows deliberately reports `FileSelection = Unsupported`. |
| Ephemeral profile | `WKWebsiteDataStore nonPersistentDataStore` | InPrivate controller options through `ICoreWebView2Environment10` | macOS is available immediately. Windows is Runtime-interface dependent and fails explicitly rather than silently using persistent storage. |
| Persistent profile path | Public WKWebsiteDataStore APIs use the system default store; `profilePath` is not a physical storage placement guarantee | `profilePath` is the WebView2 user-data directory and is created/validated | Application code must not assume persistent data lives at the requested path on macOS. |
| Resource mapping | `WKURLSchemeHandler` for `app://` | WebView2 custom-scheme registration plus `WebResourceRequested` | Both use the shared path-validation code. Windows resource loads are implemented but not GUI-E2E verified. |

## Capability Matrix

`Supported` means the backend declares that it can represent the capability. It is not a substitute for Runtime evidence where the table says verification is pending.

| Capability | macOS behavior | Windows behavior | Important difference |
| --- | --- | --- | --- |
| PersistentProfile | Supported | Supported | macOS and Windows have different `profilePath` semantics. |
| PrivateProfile | Supported | Supported only after the Runtime exposes `ICoreWebView2Environment10` | Windows can report Unsupported while the asynchronous environment is not ready. |
| ResourceMapping | Supported | Supported after session initialization is Ready | Windows requires WebView2 custom-scheme support and has not completed GUI fetch verification. |
| FileSelection | Supported | Unsupported | Windows has no chooser shim or native file dialog fallback. |
| DownloadDefault | Unsupported | Supported | On Windows the host must still provide `resolveDownload`; otherwise the event is cancelled. |
| DownloadTarget | macOS 11.3+ | Supported | Windows validates that the target is absolute and its parent directory exists. |
| Camera / Microphone | macOS 12.0+ | Unsupported in `capabilitySupport` | Windows has policy-event wiring but intentionally does not advertise a portable capability. |
| Location / Notifications / Clipboard | Unsupported | Unsupported | Both backends fail closed at the portable capability layer. |

## Observable Runtime Behavior

| Workflow | macOS | Windows | Verification state as of 2026-08-16 |
| --- | --- | --- | --- |
| Session readiness | Becomes Ready synchronously after configuration validation | Environment readiness is asynchronous and requires an STA UI thread | macOS implementation tested; Windows build/contract evidence only. |
| Data clearing | Cache, cookies, and all website data can be cleared after session readiness | The WebView2 profile becomes available only after a controller is created; clear operations wait for it or complete with an explicit error | Windows Runtime profile clearing not executed. |
| Attach and resize | Host attaches/removes the `WKWebView` NSView; attach-before-ready is queued | Controller visibility, parent HWND, and bounds are applied when attached; resize updates bounds | Windows GUI behavior not executed. |
| Navigation events | WK delegate preserves one ID across Started, Redirected, Committed, Finished, or Failed | WebView2 Runtime navigation IDs are forwarded from NavigationStarting, ContentLoading, and NavigationCompleted | Windows event sequence not GUI-E2E verified. |
| Incoming bridge | Requires current committed trusted origin, current document token, and a native main-frame check | Requires source-origin comparison, current document token, size and schema validation | WebView2 event args do not expose a main-frame flag. A hostile same-origin frame that obtains the top-level token is outside the Windows guarantee. |
| Outgoing bridge | Validates current bridge authority and uses `callAsyncJavaScript`; reports close/navigation invalidation | Posts JSON directly to the WebView2 page | Windows delivery path is implemented but not Runtime E2E verified. |
| `setHtml` | Calls `loadHTMLString` with the requested base URL | Stores the HTML as an in-memory `app://` response and navigates to that URL | `app://` base URLs require a configured mapping on both platforms. Windows page/resource behavior remains unverified. |
| Popup | WKUIDelegate transfers a child `WebViewPtr` to the host | New-window deferral creates a child and transfers it after initialization | Windows popup UI flow not executed. |
| Download | Host resolves a destination through WKDownloadDelegate | Host resolution is protected by a deferral and one-shot completion guard | Windows download UI flow not executed. |

## Windows Evidence Timeline

| Time (UTC+08:00) | Change or observation | Scope and status |
| --- | --- | --- |
| 2026-08-12 | Portable session/policy API approved; macOS implementation was the required backend | macOS contract baseline. |
| 2026-08-15 | WebView2 backend and ARM64 CMake/vcpkg integration introduced | Build and static/contract validation; not automatically proof of page behavior. |
| 2026-08-15 23:38 | Initial Windows follow-up runtime evidence recorded | Older Runtime record `151.0.4129.78` reached environment readiness only. It does not prove controller or page behavior. |
| 2026-08-16 00:06 | Readiness, pending completion, navigation, bridge, resource, popup, and download follow-ups integrated | Production wiring exists; later Runtime evidence remains limited. |
| 2026-08-16 13:34 | Follow-up specification approved | Required documentation to separate SDK/Loader, Runtime, static checks, Runtime sessions, and GUI E2E. |
| 2026-08-16 17:19 | Non-UI regression evidence recorded at revision `7259eee` | ARM64 Debug build and Windows contract/static checks passed. GUI E2E was cancelled by scope decision and remains unverified. |

## Latest Windows Runtime Record

This is a diagnostic record, not a passing E2E result.

| Build | Runtime/version | Result | What it proves | What it does not prove |
| --- | --- | --- | --- | --- |
| ARM64 Debug | `151.0.4129.86` | Controller/profile initialization timed out after 15 seconds; harness exited `1` | A Runtime version could be queried and the harness produced structured diagnostics | Successful controller creation, profile behavior, page navigation, bridge, resource mapping, popup, download, or permission behavior |
| ARM64 Release | Not reached | Process exited before main with `0xC0000135` | Release dependency deployment is incomplete in that test environment | Any WebView2 Runtime behavior |

The runtime probe has a session-close preflight. It reported that a retained view becomes closed when its session is destroyed. Popup creation was not reached, so that preflight does not validate the popup lifecycle.

## Deployment and Test Boundary

| Artifact or check | Provided by | Does it demonstrate successful page E2E? |
| --- | --- | --- |
| WebView2 SDK headers/import libraries | vcpkg package | No. It only enables compilation/linking. |
| `WebView2Loader.dll` | vcpkg/app-local deployment | No. It locates the Runtime but is not the Runtime. |
| Qt and package DLL deployment | CMake/vcpkg | No. It only makes the built application loadable. |
| Edge WebView2 Evergreen Runtime (ARM64) | Target-machine/application deployment prerequisite | Necessary but not sufficient. |
| `webview_core_tests` / `webview_windows_contract_tests` | Test suite | No. They are non-UI contract evidence. |
| `scripts/check_windows_static_contracts.py` | Static check | No. It validates source-level invariants only. |
| `webview_windows_runtime_probe` in an interactive desktop session | Runtime harness | Yes, but only for the scenarios that reach their explicit successful final result. |

## Maintainer Rules

1. Gate macOS features with the stated OS availability checks: 11.0 for outbound bridge delivery, 11.3 for download destinations, and 12.0 for camera/microphone capability.
2. On Windows, treat Environment Ready, controller/profile Ready, and page E2E as three separate evidence levels.
3. Do not equate `profilePath` on macOS with a physical storage location.
4. Do not describe WebView2 origin comparison as a main-frame check.
5. Do not claim Windows page workflows are verified until `webview_windows_runtime_probe` passes them in an interactive ARM64 desktop session with the Evergreen Runtime and all Release dependencies deployed.

## Relevant Sources

- `README.md` for Windows build prerequisites and deployment boundary.
- `docs/ARCHITECTURE.md` for session, bridge, lifecycle, and platform mapping semantics.
- `src/platform/macos/WkWebView.mm` and `src/platform/macos/WkWebViewSession.mm` for macOS availability checks and behavior.
- `src/platform/windows/WebView2View.cpp` and `src/platform/windows/WebView2Session.cpp` for Windows runtime behavior.
- `tests/WkSessionTests.mm`, `tests/WkViewTests.mm`, `tests/WebView2ContractTests.cpp`, and `tests/WebView2RuntimeProbe.cpp` for current verification coverage.
- `issues/2026-08-16_13-33-03-windows-webview2-followups/2026-08-16_13-33-03-windows-webview2-followups.csv` for dated Windows evidence and the explicit UI-E2E scope decision.
