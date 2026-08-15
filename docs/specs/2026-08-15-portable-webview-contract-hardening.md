---
mission: spec
status: approved
created: 2026-08-15
approved_at: 2026-08-15T13:28:50+08:00
---

# Portable WebView Contract Hardening

## Goal

修正 portable WebView 契约中仍依赖 macOS 同步行为的部分，使后续 WebView2 backend 可以按公共语义实现，不需要自行决定回调线程、初始化排队、popup 所有权或本地资源加载规则。

本任务直接替换有歧义的接口和实现，不保留旧 overload、兼容 shim 或双轨行为。

## Scope

- 修复异步文件选择 completion 对 `WebViewState` 的悬空引用，并统一 host completion 的存活、单次调用和线程规则。
- 让 `attachNativeView`、`load`、`setHtml` 和 session 数据清理真正经过公共初始化调度；初始化失败或关闭时，每个排队请求都有可观察结果。
- 实现可验证的 `WebResourceMapping` 语义。macOS 必须实际从映射目录解析 `app://` 资源，不能仅检查目录字符串非空。
- 收紧 popup 接管协议，使拒绝、接受和 child 所有权不能互相矛盾。
- 将下载目标选择改为异步 host completion，并规定所有 backend 回调到原生 API 前切回所属 UI 线程。
- 统一请求中的 origin 表示，只传规范化的 `scheme://host[:port]`，不在名为 `origin` 的字段中传完整页面 URL。
- 修正 capability 查询：它表示 backend/runtime 是否支持能力，不以当前 session 是否配置映射或选择哪种 mode 作为支持判断。

## Non-goals

- 不实现 Windows WebView2 backend、vcpkg 接入或 Runtime 发布。
- 不增加动态插件、运行时 backend registry 或动态加载。
- 不由 backend 打开文件或下载目录选择器。
- 不承诺 WKWebView、WebView2 和 WebKit2GTK 的原生 API 完全相同；只统一公共可观察结果。
- 不保留本任务替换掉的同步下载 resolver、旧 popup callback 或旧 completion 语义。

## Design

### Host completion lifetime and dispatch

公共异步 completion 必须满足三条规则：最多完成一次；view/session 关闭后返回明确的 Closed 或 Cancellation 结果；host 可以从任意线程完成，backend 必须在调用 Cocoa、COM 或其他原生 API 前切回创建该对象的 UI 线程。

平台 delegate 不得把 `WebViewState*`、`WkWebView*` 或其他裸 owner 指针捕获到可由 host 延迟保存的 completion 中。公共层以 `weak_ptr`、generation token 和一次性 completion state 管理存活。

文件选择 completion 返回结构化结果，区分 Selected、Cancelled、Closed 和 InvalidResult。空路径列表表示取消；路径数量和目录类型必须符合请求约束。

下载目标改为异步 completion。这样 host 可以打开自己的非阻塞 UI，WebView2 backend 也可以使用 deferral，而不需要阻塞 Qt 事件循环。

### Initialization scheduling

公共内部调度器接收成功操作和失败映射，不再只保存 `void` operation。`attachNativeView`、`load`、`setHtml` 以及 session clear 请求在 Initializing 状态排队，Ready 后按提交顺序执行。

Failed 或 Closed 时：

- attach 通过 initialization completion 报告失败，不创建或挂载原生视图；
- load 与 setHtml 发出 Failed `LoadEvent`，分配稳定的 navigation ID；
- clearCache、clearCookies、clearWebsiteData 完成 `WebsiteDataResult`，返回失败原因；
- stop 和 reload 在没有可执行 document 时保持无副作用，不进入队列。

macOS 当前同步 Ready 路径也必须调用同一调度入口。不能只在 `WebViewState` 单元测试中验证排队，而由平台方法绕开它。

### Resource mapping

`WebResourceMapping` 表示一个经过规范化的 `app://host` origin 和一个 canonical local root。session 创建时验证：scheme 必须是 `app`，host 非空，origin 不能带 path/query/fragment，root 必须存在且是目录，同一 origin 不能重复。

macOS 使用私有 `WKURLSchemeHandler` 实际提供映射资源。请求路径需要 URL decode、canonicalize，并验证结果仍位于 root 内；拒绝 `..`、symlink escape、目录读取和不存在文件。响应提供确定的 MIME type，错误通过 WebKit scheme task 返回。

`setHtml` 的 app base URL 只有在匹配有效 mapping 时才能加载。相对 CSS、JavaScript、图片和 fetch 必须从该 mapping 得到真实内容。若当前 backend 无法提供这一行为，`ResourceMapping` 返回 Unsupported，`setHtml` 发出 Failed event。

### Popup ownership

用单一结果类型替换 `NewWindowDisposition` 与可移动 `WebViewPtr&` 的组合。结果只能表示两种有效状态：Rejected，不携带 child；Accepted，host 已接管 child。公共 API 不允许“移动 child 后返回 Rejected”或“返回 Accepted 但 child 仍由 backend 持有”。

macOS 同步 popup delegate 仍可保留原生 configuration，但它只能存在于平台实现内部。公共 callback 完成前，host 必须把 child 放入最终 Qt 容器并显式 attach。

### Capability semantics

`capabilitySupport` 回答 backend/runtime 能否实现能力，与当前 session 配置分离。例如，macOS backend 支持 resource mapping 时，即使当前 `resourceMappings` 为空也返回 Supported；支持 persistent 和 ephemeral profile 时，两项都返回 Supported。

配置是否合法由 session 初始化结果报告。能力存在但本次 session 未启用，不得伪装成 Unsupported。

### Normalized origins

`PermissionRequest`、`FileSelectionRequest`、`DownloadRequest` 和 bridge 授权使用同一个公共 origin 规范化函数。origin 只包含小写 scheme、host 和显式有效端口，不包含用户信息、path、query 或 fragment。需要完整页面地址的场景使用单独的 `documentUrl` 字段。

Architecture recall was unavailable: `lite-arch-recall` is not installed and the repository has no `docs/adr/` records.

## Compatibility

这是一次源码级替换。删除同步 `resolveDownload`、旧 popup callback 和只接收路径列表的 file completion。调用方必须迁移到新的结构化异步结果，不提供 forwarding overload。

## Security

- host completion 不得在 owner 销毁后解引用平台或公共状态。
- completion 必须防止重复调用和跨线程直接进入 Cocoa/COM。
- resource mapping 必须阻止路径穿越、symlink escape 和映射 root 之外的文件访问。
- origin 规范化必须由公共实现完成，不能由各 backend 自行拼接字符串。
- popup 接管失败时，child 必须关闭，不能留下不可见但仍有 session 权限的页面。

## Acceptance Criteria

- host 保存 file/download completion，在 view 关闭或析构后调用，不发生 UAF，不调用已释放的原生 handler，并返回或记录 Closed/Cancelled；重复调用只生效一次。
- 从 worker thread 调用 host completion 时，Cocoa/WebKit completion 在创建 view 的 UI thread 执行；测试能够观察线程切换。
- macOS 的 attach、load、setHtml 经过公共 initialization scheduler；模拟 Initializing 后转 Ready、Failed、Closed 时，排队顺序和失败结果均有测试覆盖。
- session 数据清理在初始化失败或关闭时完成失败结果，不静默丢弃 callback。
- `app://` 映射能够加载 HTML 引用的相对 CSS、JavaScript 和图片；不存在文件、`..`、percent-encoded traversal 和 symlink escape 被拒绝。
- 无效或重复 mapping 使 session 初始化 Failed；错误包含具体字段和 origin，不只写平台日志。
- capability 查询不随当前 session 是否配置 mapping 或选择 persistent/ephemeral mode 改变；Runtime 版本限制仍可返回 Unsupported。
- popup callback 的类型系统不能表达 ownership 与 disposition 矛盾状态；拒绝、接受、host 缺失和 host 销毁 child 都有测试。
- file、download、permission 和 bridge 请求中的 origin 均为同一规范化结果；完整 document URL 只出现在独立字段。
- 公共头文件不包含 WebKit、WebView2、Cocoa 或 COM 类型；demo 和测试不转型到具体 backend。
- macOS core/session 测试通过；view、popup、resource mapping 和异步 completion 测试在有 WindowServer 的会话中通过。无 WindowServer 时必须记录受限项，不能把静态检查写成真实 GUI E2E。
- CMake 和预处理宏仍是唯一 backend 选择机制，仓库中不存在动态 plugin loader 或 runtime backend registry。
