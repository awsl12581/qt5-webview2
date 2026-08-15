---
mission: spec
status: approved
created: 2026-08-15
approved_at: 2026-08-15T15:22:43+08:00
---

# Windows WebView2 Backend

## Goal

在 Windows 上使用 vcpkg 提供的 WebView2 SDK `1.0.3800.47` 实现 `system_webview` backend。Windows 实现必须消费现有公共 WebView 契约，与 macOS 对外行为保持一致；平台差异只能通过 capability 和明确的失败结果暴露。

本任务使用 CMake 和预处理宏静态选择 backend，不引入动态插件、运行时 backend registry 或动态加载。

## Scope

- 接入 `unofficial::webview2::webview2` CMake target 和 ARM64 Windows preset。
- 实现 WebView2 session/profile、environment/controller 异步初始化和 Qt Widget 承载。
- 将 WebView2 navigation、popup、bridge、download、permission、close 和 website data 事件映射到公共类型。
- 使用公共 `InitializationScheduler`、`HostCompletionGuard`、`normalizedOrigin` 和 `ResourceMapping`，不在 Windows backend 重造同类规则。
- 使用 WebView2 custom scheme registration 和 `WebResourceRequested` 提供 `app://` 映射资源；无法满足 Runtime 条件时报告 `Unsupported` 或初始化失败。
- 实现 popup 的 deferral 和按值 child ownership 转移。
- 实现异步 download resolver：支持 `Cancel`、`BrowserDefault` 和 `TargetPath`。
- 明确 Windows `FileSelection` 为 `Unsupported`。WebView2 SDK 没有可接管 HTML `<input type=file>` 的公共 chooser 事件；不注入 JavaScript shim，不创建额外文件选择器。
- 在 Windows backend 的文件选择能力判断和相关事件边界保留 `TODO(webview2-file-selection)` 注释，便于 SDK 未来提供接管接口时定位。
- 增加 Windows 单元、集成和 GUI 验证，并记录 WebView2 Runtime 缺失、无桌面会话和无 GPU 等受限条件。

## Non-goals

- 不修改公共 `src/webview` 接口以适配 WebView2 私有类型。
- 不实现 WebView2 SDK 之外的 HTML file chooser shim。
- 不把 WebView2 自带 file picker UI 宣称为 host-owned `FileSelection` 能力。
- 不实现固定版本 WebView2 Runtime 安装器、Evergreen Runtime 发布器或企业部署策略；只检测 Runtime 是否可用并返回明确初始化错误。
- 不改变 macOS backend 的行为，不保留旧同步 resolver、旧 popup callback 或兼容 overload。
- 不新增隐式 HTTP server，不把 `app://` URL 改写为 `https://` URL。

## Design

### Build and static backend selection

`cmake/platform/windows.cmake` 使用：

```cmake
find_package(unofficial-webview2 CONFIG REQUIRED)
target_link_libraries(system_webview PRIVATE unofficial::webview2::webview2 ole32 shlwapi)
```

`WebViewFactory.cpp` 通过 `_WIN32` 分支构造 `WebView2Session`。WebView2、COM、HWND 类型只能出现在 `src/platform/windows`；公共头、demo 和跨平台测试不得包含或转型到这些类型。

使用现有 `windows-msvc-arm64-debug` 和 `windows-msvc-arm64-release` presets，确认 vcpkg target 为 `arm64-windows`，并验证 `WebView2LoaderStatic.lib` 或等价导入库来自 vcpkg target。

### Session and initialization

在创建 session 的 Qt UI 线程初始化 COM STA，并调用 `CreateCoreWebView2EnvironmentWithOptions`。

- Persistent 使用 `profilePath` 作为 user data folder。
- Ephemeral 使用 WebView2 InPrivate profile/controller option；Runtime 不支持时 session 为 `Failed`，不得静默使用 persistent 临时目录。
- Runtime 不存在、profile 不可写、COM/HRESULT 失败都转换为 `InitializationResult::Failed`，带可诊断错误。
- Environment、controller 和 profile 的异步完成统一进入 `InitializationScheduler`。
- `clearCache`、`clearCookies`、`clearWebsiteData` 使用 profile browsing-data API，并通过 scheduler 返回 `WebsiteDataResult`。

### Qt native hosting

`WebView2View` 持有私有 `NativeViewHost : QWidget` 和 `ICoreWebView2Controller`。controller 使用 host widget 的 HWND，`attachNativeView()` 才设置 parent、bounds 和可见状态；`detachNativeView()` 隐藏 controller。resize 只同步 bounds，不根据 visibility、parent、默认尺寸或事件循环时序自行 attach。

所有 COM event token 由 RAII 成员注销。view close 先失效 document generation、清理 callbacks 和 event handlers，再停止导航、关闭 controller、释放 COM 对象。

### Completion lifetime and thread dispatch

Windows backend 复用 `HostCompletionGuard` 和 weak `WebViewState`。host 可在任意线程完成 file/download completion，但 backend 进入 COM API、deferral 或 controller 前必须回到创建 view 的 Qt UI 线程。重复、关闭后、owner 销毁后的 completion 只生效一次并返回 Closed/Cancelled 结果。

### Navigation and bridge

`NavigationStarting` 先转换为 `NavigationRequest` 并调用 policy；`SourceChanged`/`ContentLoading`/`NavigationCompleted` 转换为公共 LoadEvent，主框架 redirect 保持稳定 navigation ID。`OpenExternally` 只通知 host，不调用 ShellExecute。

document-start 注入公共 bridge transport。`WebMessageReceived` 必须校验 main frame、当前 committed origin、document token、版本、schema 和消息大小。sendMessage 使用 WebView2 JSON API，不能拼接未转义的 JavaScript。

### Resource mapping

使用 WebView2 custom scheme registration 保留 `app://host/path` 的公共 URL 语义；通过 `WebResourceRequested` 调用公共 mapping resolver，并用 `CreateWebResourceResponse` 返回确定 MIME 的 stream。

必须拒绝 URL decode 后的 `..`、percent-encoded traversal、symlink escape、目录和不存在文件。有效 mapping 才允许 `setHtml` 使用 `app://` base URL。Runtime 缺少 custom scheme 能力时，`ResourceMapping` 为 `Unsupported`，配置 mapping 的 setHtml 发出 Failed event，不启动隐式 HTTP server。

### Popup

`NewWindowRequested` 先执行 policy，再取得 WebView2 deferral。允许时创建同一 session 的 child view，Ready 后将 child 的 CoreWebView2 设置到 request args，host 通过公共按值 callback 接管 child；host 丢弃 child、callback 缺失或 session 关闭时关闭 child 并取消 deferral。

### Download, permission and file selection

- `DownloadStarting` 先构造含 `origin` 和 `documentUrl` 的 `DownloadRequest`，policy 允许后调用异步 `resolveDownload`。
- `Cancel` 取消下载；`BrowserDefault` 保留 WebView2 默认路径；`TargetPath` 设置明确的 result file path。
- `PermissionRequested` 只映射公共 `PermissionDecision`，不显示 backend 自己的权限 UI。
- `FileSelection` capability 固定为 `Unsupported`。不得调用系统文件对话框，也不得用 JS shim 冒充 host-owned completion。
- 在 `WebView2Session::capabilitySupport` 的 FileSelection 分支和 file chooser 相关隔离位置加入：

```cpp
// TODO(webview2-file-selection): revisit when WebView2 exposes a host chooser event.
```

### Capability semantics

capability 反映 WebView2 Runtime/interface 实际能力，不依赖当前 options 是否配置 mapping 或选择哪种 profile mode。Runtime 版本或接口不支持时返回 `Unsupported`；配置错误通过 session initialization failure 表达。

## Security

- 所有 COM callbacks 使用 weak state、generation 和一次性 guard，禁止捕获裸 view/session owner。
- origin 统一使用公共 normalizer；bridge、permission、download、file request 不自行拼接 origin。
- resource mapping 必须经过 canonical root 和 symlink escape 检查。
- popup 接管失败必须关闭 child，不能留下仍有 session 权限的隐藏页面。
- WebView2 Runtime 缺失或接口不足不能静默降级到另一个 backend。

## Acceptance Criteria

- Windows ARM64 Debug/Release preset 能找到 vcpkg WebView2 target，并完成 clean configure/build。
- `createWebViewSession` 在 Runtime 可用、Runtime 缺失、profile 失败和关闭竞态下均返回明确初始化结果。
- Qt widget attach/detach/resize 可观察，controller 不因可见性或 parent 变化自行挂载。
- navigation、redirect、failure、stop、reload 和 close 产生与公共契约一致的结果和 navigation ID。
- bridge 只接受当前 main-frame origin 和 document token 的合法消息；navigation/close 后旧消息被拒绝。
- persistent 和 ephemeral profile 行为分别可验证；clear cache/cookies/website data 返回明确 completion。
- popup 的 Allow/Cancel、child 丢弃、host 缺失、session close 和 deferral 完成均有测试。
- download 的 Cancel、BrowserDefault、TargetPath、重复 completion、关闭后 completion 和 worker-thread completion 均有测试。
- Windows `FileSelection` capability 为 `Unsupported`，代码中存在 `TODO(webview2-file-selection)`，没有 JS shim、`IFileDialog` 或 backend 自有 chooser 实现。
- 有效 `app://` mapping 可加载 HTML 相对资源和 fetch；不存在文件、目录、`..`、percent-encoded traversal、symlink escape 被拒绝；不支持 custom scheme 时返回 Unsupported/Failed。
- origin、documentUrl、capability、popup ownership 和 host completion 均直接复用公共契约。
- 公共头、demo 和跨平台测试不包含 WebView2、COM、HWND；仓库无动态 plugin loader、registry 或 `LoadLibrary`。
- Windows core/session/view 集成测试在有桌面 WebView2 Runtime 的 ARM64 环境通过；缺少 Runtime、桌面会话或可视化环境时，测试报告必须明确受限，不得把静态检查写成 GUI E2E。
