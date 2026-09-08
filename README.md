# System WebView

A Qt 5 host for operating-system WebViews. The current backend uses macOS `WKWebView`; the public session, page, and policy contracts are designed to map to WebView2 and WebKit2GTK without exposing native types.

Build with Homebrew Qt 5:

```sh
cmake --preset default-debug
cmake --build --preset default-debug
ctest --preset default-debug
./build/debug/samples/demo/system_webview_demo
```

## Install and consume from CMake

The default install prefix is the repository's `install/` directory. It can
be overridden in the normal CMake way with `-DCMAKE_INSTALL_PREFIX=...`:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DSYSTEM_WEBVIEW_BUILD_SAMPLES=OFF
cmake --build build/release
cmake --install build/release
```

This creates an install tree containing `include/`, `lib/`, and
`lib/cmake/system_webview/`. A consumer can then use the installed target:

```cmake
find_package(system_webview CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE system_webview::system_webview)
```

Configure the consumer with `-DCMAKE_PREFIX_PATH=/path/to/system-webview/install`.

The real WKWebView page integration test needs access to macOS WindowServer. In a headless or restricted sandbox, run the core and session suites there and run `webview_macos_view_tests` in a logged-in GUI session.

## Windows ARM64 and AMD64

Open a matching VS 2026 developer prompt from PowerShell. Use `-arch=arm64 -host_arch=arm64` for ARM64, or `-arch=amd64 -host_arch=amd64` for AMD64:

```powershell
cmd.exe /k '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=arm64'
```

For AMD64, replace the architecture arguments with:

```powershell
cmd.exe /k '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64'
```

Configure and build from that prompt:

```bat
cmake --preset windows-msvc-arm64-debug
cmake --build --preset windows-msvc-arm64-debug
cmake --preset windows-msvc-arm64-release
cmake --build --preset windows-msvc-arm64-release

cmake --preset windows-msvc-amd64-debug
cmake --build --preset windows-msvc-amd64-debug
cmake --preset windows-msvc-amd64-release
cmake --build --preset windows-msvc-amd64-release
```

Run the application demo from its sample directory (the similarly named
`webview_windows_runtime_probe.exe` is an automated test and intentionally shows
an empty host window):

```powershell
.\build\windows-msvc-amd64-debug\samples\demo\system_webview_demo.exe
```

The ARM64 presets use the `arm64-windows` triplet and the AMD64 presets use `x64-windows`. Both use the sibling vcpkg checkout for Qt, WebView2, and their dependencies:

```powershell
vcpkg install qt5-base:arm64-windows webview2:arm64-windows
vcpkg install qt5-base:x64-windows webview2:x64-windows
```

vcpkg supplies the WebView2 SDK/Loader and linked Qt dependencies. This does not install the Microsoft Edge WebView2 Evergreen Runtime on the target machine; a Runtime matching the target architecture remains an application deployment prerequisite.

`webview_core_tests` and `webview_windows_contract_tests` are compile/integration evidence. `webview_windows_runtime_probe` is GUI Runtime evidence only when it reaches each named scenario in an interactive desktop session and exits successfully. A missing Runtime, missing DLL, controller timeout, or restricted desktop must be reported with the failed scope and manual rerun command, not as E2E success.

The library requires explicit profile ownership:

```cpp
webview::WebViewPolicyConfig config;
config.allowedAppHosts.insert(QStringLiteral("dashboard"));
auto policy = webview::createDefaultWebViewPolicy(std::move(config));
webview::WebViewSessionOptions options;
options.mode = webview::SessionMode::Ephemeral;
auto session = webview::createWebViewSession(std::move(options), std::move(policy));
webview::WebApplicationOptions app;
app.id = QStringLiteral("dashboard");
app.source = webview::LocalBundle { QStringLiteral("/path/to/vite/dist") };
auto dashboard = session->createApplication(std::move(app));
auto view = session->createWebView(parent);
view->attachNativeView();
view->open(dashboard, QStringLiteral("/orders/42"));
```

`LocalBundle` is the normal production source for a Vite `dist` directory. During development, use `DevelopmentServer { QUrl("http://127.0.0.1:5173") }`; a deployed site uses `RemoteOrigin`. All three sources use `view->open(application, route)`. The demo retains one session for all tabs and maps the allowlisted `https://example.com/` popup to a `QTabWidget` tab. See [the architecture guide](docs/ARCHITECTURE.md) for lifecycle, bridge, cleanup, platform mappings, and migration semantics.

For a development server, add its exact origin to `WebViewPolicyConfig::trustedDevelopmentOrigins`; HTTP remains denied for every other origin.
