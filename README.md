<div align="center">

# System WebView

A small Qt 5 library for embedding the operating system's web view in a C++ application.

<p>
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&amp;logoColor=white" alt="C++17"></a>
  <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-5.15-41CD52?logo=qt&amp;logoColor=white" alt="Qt 5.15"></a>
  <img src="https://img.shields.io/badge/Windows-WebView2-0078D4?logo=windows&amp;logoColor=white" alt="Windows WebView2">
  <img src="https://img.shields.io/badge/macOS-WKWebView-000000?logo=apple&amp;logoColor=white" alt="macOS WKWebView">
</p>

**English** | [简体中文](README.zh-CN.md)

</div>

<p align="center">
  <img src="docs/image.png" alt="System WebView demo">
</p>

System WebView presents one Qt-friendly API over Microsoft WebView2 on Windows and WKWebView on macOS. Native SDK types stay inside the platform backends, so application code deals only with Qt values and the public C++ interfaces.

## What it provides

- Persistent and ephemeral browser sessions
- Local bundles, development servers, and remote origins
- Schema-checked JSON messages between C++ and the page
- Navigation, popup, permission, download, and file-selection policy hooks
- Native view attachment and resize handling inside Qt widgets
- CMake package installation through `system_webview::system_webview`

The demo also shows a frameless window whose title bar and controls live in the web page. On Windows, DWM supplies the rounded corners and desktop shadow.

## Platform support

| Platform | Backend | Toolchain | Notes |
| --- | --- | --- | --- |
| Windows AMD64 | Microsoft WebView2 | MSVC v143, Qt 5.15 | Requires the WebView2 Evergreen Runtime |
| Windows ARM64 | Microsoft WebView2 | MSVC, Qt 5.15 | Uses the `arm64-windows` vcpkg triplet |
| macOS | WKWebView | Apple Clang, Qt 5.15 | Uses the system WebKit framework |

Linux is not implemented. WebKit2GTK is documented as a future mapping only.

## Build on Windows

The presets expect a sibling vcpkg checkout at `../vcpkg`. Install Qt and the WebView2 SDK for your architecture first:

```powershell
..\vcpkg\vcpkg install qt5-base:x64-windows webview2:x64-windows
```

For ARM64, replace `x64-windows` with `arm64-windows`.

Open an MSVC developer prompt that matches the target architecture, then configure, build, and test:

```powershell
cmake --preset windows-msvc-amd64-debug
cmake --build --preset windows-msvc-amd64-debug
ctest --preset windows-msvc-amd64-debug --output-on-failure
```

Run the demo:

```powershell
.\build\windows-msvc-amd64-debug\samples\demo\system_webview_demo.exe
```

The vcpkg package supplies the WebView2 SDK and Loader. The target computer still needs a matching Microsoft Edge WebView2 Evergreen Runtime.

## Build on macOS

The macOS presets expect Homebrew Qt 5 at `/opt/homebrew/opt/qt@5`:

```sh
brew install cmake ninja qt@5
cmake --preset macos-appleclang-debug
cmake --build --preset macos-appleclang-debug
ctest --preset macos-appleclang-debug --output-on-failure
./build/macos-appleclang-debug/samples/demo/system_webview_demo
```

Tests that create a real WKWebView need a logged-in GUI session with access to WindowServer.

## Use the library

Create one session, register an application source, and attach a view to a Qt widget:

```cpp
webview::WebViewPolicyConfig policyConfig;
policyConfig.allowedAppHosts.insert(QStringLiteral("dashboard"));

webview::WebViewSessionOptions sessionOptions;
sessionOptions.mode = webview::SessionMode::Ephemeral;

auto session = webview::createWebViewSession(
    std::move(sessionOptions),
    webview::createDefaultWebViewPolicy(std::move(policyConfig)));

webview::WebApplicationOptions appOptions;
appOptions.id = QStringLiteral("dashboard");
appOptions.source = webview::LocalBundle { QStringLiteral("/path/to/vite/dist") };
appOptions.bridgeAccess = webview::BridgeAccess::Allowed;

auto application = session->createApplication(std::move(appOptions));
auto view = session->createWebView(parentWidget);
view->open(application, QStringLiteral("/orders/42"));
view->attachNativeView();
```

`LocalBundle` is intended for packaged web assets such as a Vite `dist` directory. Use `DevelopmentServer` for a local development URL and `RemoteOrigin` for a deployed site. Development HTTP origins must be listed in `trustedDevelopmentOrigins`.

Keep the session alive for as long as any of its applications or views are in use. A `WebViewPtr` also owns the native view lifecycle, so detach and close it before destruction when your host container requires explicit cleanup.

## Install for another CMake project

The repository defaults to an `install/` prefix. You can choose another prefix with the normal CMake option:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DSYSTEM_WEBVIEW_BUILD_SAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/system-webview
cmake --build build/release
cmake --install build/release
```

Consume the installed package like this:

```cmake
find_package(system_webview CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE system_webview::system_webview)
```

Point `CMAKE_PREFIX_PATH` at the chosen install prefix when configuring the consumer.

## Documentation

- [Architecture and lifecycle](docs/ARCHITECTURE.md)
- [Platform behavior matrix](docs/platform-version-and-behavior-matrix.md)
- [Architecture decisions](docs/adr/README.md)
- [Design specifications](docs/specs/)

The platform behavior matrix separates implemented code from desktop runtime evidence. Read it before relying on a capability that differs between WKWebView and WebView2.
