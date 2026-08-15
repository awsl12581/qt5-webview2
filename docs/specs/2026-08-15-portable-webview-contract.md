---
mission: spec
status: approved
created: 2026-08-15
approved_at: 2026-08-15T10:20:00+08:00
---

# Portable WebView Contract

## Goal

把现有 macOS WKWebView 实现中已经被应用、demo 或测试直接依赖的行为提到 `src/webview` 公共契约中。完成后，应用代码只能依赖 `IWebView`、`IWebViewSession`、`WebViewPolicy` 和公共 host 协议；`WkWebView` 必须能作为 `IWebView` 的完整替代实现，不需要类型转换。

这项工作为 Windows WebView2 backend 准备共同的静态编译接口。每个平台仍由预处理宏和 CMake 平台分支选择，不引入动态插件、运行时 backend 注册或动态加载。

## Scope

- 在 `src/webview` 定义原生视图挂载、初始化状态、popup 接管、文件选择、下载目标、资源映射、navigation/bridge 生命周期所需的公共类型和接口。
- 删除 demo、测试和公开头文件中对 `platform/macos/WkWebView.h` 与 `platform/macos/WkWebViewSession.h` 的业务依赖。
- 将 macOS backend 改为仅实现公共接口和公共状态机，不改变其已验证的导航、popup、bridge、session 与关闭语义。
- 将现有 macOS 测试改为通过公共接口断言可观察行为；保留原生测试仅用于 backend 内部，且不暴露为公共 API。
- 更新架构文档，说明 host、公共层和平台层各自负责的决策。

## Non-goals

- 不在本任务中实现 Windows WebView2 backend、vcpkg 接入或 Runtime 发布。
- 不提供旧 API、旧具体类调用、兼容 overload、别名或迁移 shim。
- 不把 Qt tab/window、系统文件选择器、下载目录、浏览器 Runtime 路径或资源目录的选择放入 backend。
- 不引入动态插件或运行时平台发现。

## Design

### Compile-time platform selection

`WebViewFactory.cpp` 继续使用平台预处理宏直接构造当前平台的 session。CMake 只编译当前平台的 backend 源文件并链接对应原生库：macOS 编译 WKWebView 源文件，Windows 后续编译 WebView2 源文件。公共头文件不得包含 WebKit 或 WebView2 类型。

### Native attachment lifecycle

`IWebView` 增加显式的原生宿主生命周期。host 在 `widget()` 已进入最终 Qt 布局并获得最终 geometry 后调用 `attachNativeView()`；在宿主表面即将销毁或重新安置前调用 `detachNativeView()`。

backend 不根据可见性、parent、默认尺寸或事件循环时序猜测何时挂载。macOS 保留其内部 `NativeViewHost` 与 NSView 操作，Windows 后续以 HWND 和 WebView2 controller 实现相同公共语义。

删除业务代码对 `WkWebView::attachNativeView()` 的直接调用和类型转换。普通 view 与 popup view 必须使用相同的 `IWebView` attach 路径。

### Initialization and capability state

公共层定义 session/view 初始化状态和失败结果，至少区分 `Initializing`、`Ready`、`Failed`、`Closed`。初始化完成或失败通过公共 completion 通知；错误信息不得只留在平台日志中。

这使同步创建 WKWebView 与异步创建 WebView2 Environment/Controller 都能实现同一契约。初始化期间收到的 `load`、`setHtml`、`attachNativeView` 和网站数据清理请求必须有统一规则；本任务选择由公共层排队到 Ready，初始化失败时以明确结果结束，不运行嵌套事件循环，也不让 backend 私自丢弃请求。

`IWebViewSession` 的 capability 查询扩展到 profile/private mode、文件选择、下载和资源映射等会受平台与 Runtime 限制的能力。Unsupported 必须显式返回，不能静默降级。

### Shared lifecycle and bridge state

从 `WkWebView.mm` 的 `NativeState` 抽出平台无关的状态机，放入 `src/webview` 内部实现：

- closed 与 callback dispatch guard；
- navigation generation 和 `MessageError::NavigationChanged`；
- 当前 committed main-frame URL；
- 公共 navigation ID 的分配与完成/失败清理；
- bridge document token、可信 origin 与当前 document 授权状态。

平台 backend 只把 WK delegate 或后续 WebView2 event handler 翻译成状态机输入。原生 navigation pointer/event token、WK user-content controller、WebView2 COM event token 仍留在各自平台目录。

公共 bridge 协议继续使用版本化 JSON envelope。每个 backend 都必须在调用 host message callback 前执行主 frame、已提交 origin、当前 document token、消息大小和 schema 校验。平台层不得默认信任 URL、frame 或页面脚本。

### Popup host handoff

popup 的共同语义是：policy 先决定是否允许；backend 在同一 session 创建 child `IWebView`；host 接收 child 所有权、放入 Qt tab 或窗口并完成布局；host 显式 attach；backend 完成原生 popup 接管。

公共 host 协议必须表达 host 是否接管 child 和何时结束 popup 请求。host 缺失、拒绝或未接管时，backend 取消原生 popup。`void*` configuration 和 `std::function<void*(void*, ...)>` 仅能作为 macOS backend 的内部细节，不能跨越公共层。

### Host-owned file selection and download destinations

公共层新增文件选择请求与完成回调。请求包含 origin、是否允许多选、是否允许目录等可移植信息；host 返回选中的本地路径或取消。删除 macOS backend 直接创建 `NSOpenPanel` 的行为。Windows backend 后续也不得直接打开系统文件对话框。

下载由 host resolver 返回取消、浏览器默认处理或明确目标路径。仅有 `DownloadDecision::Allow` 不再表示 backend 可以自行选择保存目录。没有 resolver 或 resolver 没有给出允许结果时，下载取消。

### Resource mapping and HTML loading

session 创建选项包含显式资源映射：可信 origin 对应的本地目录或 host 提供的资源 resolver。`setHtml` 只在提供的 base URL 通过 policy 且当前 backend 能表示该 origin 时加载。

backend 不改写 `app://` URL、不启动隐式本地 HTTP server、不猜测资源目录。macOS 保留其可用的 base URL 实现；Windows 后续根据该公共映射选择 WebView2 virtual host mapping 或报告 Unsupported。

### Removal of platform test APIs

删除 `WkWebView` 与 `WkWebViewSession` 为测试公开的 native configuration、document token、native attachment 和 native size 查询方法。测试改为验证公共接口的生命周期、host attach、popup 所有权、bridge 和关闭结果。若某个 macOS 原生断言仍有价值，它必须位于平台测试内部，不能扩大公共接口。

Architecture recall was unavailable: `lite-arch-recall` is not installed and the repository has no `docs/adr/` records.

## Acceptance Criteria

- 应用、demo 和跨平台测试只包含 `src/webview` 公共头文件；不存在 `static_cast<WkWebView*>`、`static_cast<WkWebViewSession*>` 或业务层对 `platform/macos` 的 include。
- `IWebView` 的公共 attach/detach 生命周期足以让普通 tab 与 popup tab 在最终 Qt geometry 上首次挂载；macOS demo 不依赖具体 backend 类。
- 公共初始化状态能表达 Ready、Failed 与 Closed；macOS 使用它而不改变现有成功路径，且为异步 WebView2 初始化预留同一行为。
- navigation generation、committed main-frame URL、bridge token、callback-after-close guard 和公共 navigation ID 不再定义在 macOS-only state 中。
- bridge 仍拒绝 iframe、未提交页面、非可信 origin、旧 document token、超限或不符合 schema 的消息。
- popup 仍在 policy 允许后才创建，host 未接管时取消；已接管 child 与 parent 共享 session 语义，且 host 显式控制首次原生挂载时机。
- 文件选择和下载目标由 host 提供的公共协议决定；macOS backend 不再直接创建 `NSOpenPanel`，也不自行选择下载目录。
- `setHtml`、资源 origin 和本地资源映射的限制在公共接口和文档中明确，backend 不做隐式 URL 或资源来源推断。
- 所有已删除的具体类 testing API 没有 public replacement；测试从公共可观察行为获得等价覆盖。
- macOS core、session 和 view 测试通过；macOS GUI 集成测试继续在有 WindowServer 的会话中验证 popup 首次挂载和 bridge 行为。
- CMake 仍按 `APPLE`、`WIN32` 等编译期条件选择 backend，没有动态插件、运行时 backend registry 或 dynamic loading。
