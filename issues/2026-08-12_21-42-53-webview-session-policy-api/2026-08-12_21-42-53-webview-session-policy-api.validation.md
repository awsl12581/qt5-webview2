# macOS Integration Validation

- Run completed: 2026-08-13 09:24 CST
- Host: macOS 26.6.1 (25G76), arm64
- Compiler: Apple clang 21.0.0
- CMake: 4.3.3
- Clean build directory: `/tmp/system-webview-mission.iLoRWK`

## Commands and results

```sh
cmake -S . -B /tmp/system-webview-mission.iLoRWK -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5 \
  -DBUILD_TESTING=ON \
  -DSYSTEM_WEBVIEW_BUILD_SAMPLES=ON
```

Result: passed. CMake configured and generated the clean build tree.

```sh
cmake --build /tmp/system-webview-mission.iLoRWK -j 4
```

Result: passed, 16/16 build steps. The only warnings were the already documented macOS 12 deprecations for `WKProcessPool`; the implementation does not claim modern process isolation.

```sh
ctest --test-dir /tmp/system-webview-mission.iLoRWK \
  --output-on-failure \
  -R 'webview_core_tests|webview_macos_session_tests'
```

Result: passed, 2/2 tests in 1.78 seconds.

```sh
ctest --test-dir /tmp/system-webview-mission.iLoRWK \
  --output-on-failure \
  -R webview_macos_view_tests
```

Result: passed, 1/1 test in 3.16 seconds. This command ran in a logged-in WindowServer session because a restricted/headless process cannot create the native view.

## Behavioral matrix

| Scenario | Real path exercised | Result |
|---|---|---|
| Session storage | Two configurations from one persistent session and one ephemeral session | Shared persistent data store/process-pool identity, separate controllers, non-persistent ephemeral store |
| Website data cleanup | `WKWebsiteDataStore` cache, cookie, and all-data removal callbacks | Passed |
| Lifecycle | HTML load, loopback redirect, reload, unavailable port, stop | Stable navigation IDs; redirect and failure observed; reload issued a second request; stopped slow response did not finish |
| Navigation policy | HTTPS/app defaults, rejected `javascript:`, loopback HTTP override, redirect request context | Initial request reported `isRedirect=false`; redirected URL reported `true`; rejected scheme did not start navigation |
| Bridge authority | Trusted main-frame transport, iframe raw handler, same-origin stale token | Trusted message accepted; iframe and stale document rejected |
| Bridge envelope | Version, type, schema, size, hostile outbound string | Invalid envelopes rejected; hostile string arrived as data without execution |
| Popup policy/session | Default rejected popup and explicitly allowed popup | Rejection produced no child; allowed child shared the data store, received a separate controller, and registered with the session |
| Permission mapping | Production `decideNativePermission` path used by media and file-picker delegates | Deny invokes policy and maps to native deny; unsupported also maps fail-closed |
| Download response | Loopback `application/octet-stream` response through `decidePolicyForNavigationResponse` | Policy received `/download` and cancelled it |
| Close race | Close immediately after starting a slow network navigation | No lifecycle callback fired after close |
| Session destruction | Retained root and popup after destroying their session | Both became closed/inert; later load did not call policy; later messages returned `Closed` |

## Explicit limits

- A script-generated file-input click is not a WebKit user activation, so it does not call `runOpenPanelWithParameters`. The shared production permission decision path is tested directly, but an allowed native file panel and user selection are not automated.
- The download test exercises the real non-displayable response callback and default cancellation. Selecting a download destination and writing a file are outside this mission's universal download-manager scope.
- Camera and microphone mapping and capability discovery are built and tested, but macOS privacy prompts and physical-device capture are not automated.
- WebView2 and WebKit2GTK remain documented contract mappings only; this report is evidence for the macOS backend.
