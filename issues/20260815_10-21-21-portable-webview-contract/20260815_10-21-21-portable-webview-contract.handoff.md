# Portable WebView Contract - 施工交工单

> 独立性: true | mode=reviewer-subagent | requested_model=gpt-5.6-sol
> 日期: 2026-08-15

## Overall Conclusion

本轮批准范围部分通过：公共契约、macOS 回填、host-owned 边界、资源映射、测试迁移、文档和编译期平台选择均已落地。core/session 测试通过；GUI view 测试因当前环境没有 WindowServer/screens 在创建窗口前中止，因此没有声称真实 view/popup/bridge E2E。独立 reviewer 还发现 macOS 文件选择 completion 缺少 one-shot/close-safe guard，已追加 follow-up issue。

## 先看结论

见 Overall Conclusion。

## 交工单告诉你什么

见 Artifact Role。

## 你现在可以确定什么

见 Reader Answers。

## 交付物与证据

| spec 目标 | 状态 | 直接答案 | 证据 | 边界 |
|---|---|---|---|---|
| 应用、demo、跨平台测试只依赖公共契约 | pass | 是。公共路径无 WkWebView/WkWebViewSession 具体类型依赖；macOS backend 内部保留必要私有转换。 | `src/webview/`, `samples/demo/`, `tests/` 静态扫描；core/session 通过 | GUI 运行未完成 |
| 公共接口表达生命周期与 host-owned 决策 | partial | 大部分已表达；异步 ready operation 的集成消费和失败结果仍需 follow-up。 | `IWebView.h`, `WebViewTypes.h`, `WebViewState.cpp` | follow-up `HOST-03` |
| macOS 既有语义回归 | partial | core/session 通过；view suite 受无屏幕环境限制。 | `ctest`: 2/3；view test: `Cannot create window: no screens available` | 未观察真实 GUI geometry/bridge |
| backend 只在编译期选择 | pass | 是。CMake 和宏选择平台源，无 runtime registry/plugin loader。 | `CMakeLists.txt`, `WebViewFactory.cpp`, static scan | 仅验证当前 macOS 配置 |

## Artifact Role

本交工单服务于批准 spec 的公共契约抽离和 macOS 回填。它不能证明 Windows WebView2 backend、vcpkg 接入或 Runtime 发布已经完成。

## Reader Answers

| question_id | 判定 | 直接答案 | 可信度 |
|---|---|---|---|
| OUTCOME-001 | pass | 公共、demo、跨平台测试路径已摆脱具体类型；唯一具体转换在 macOS backend 私有 popup 路径。 | high |
| OUTCOME-002 | partial | 公共接口覆盖初始化、attach/detach、navigation、bridge、popup 和 host-owned 决策；异步 ready/failure 集成和 file completion guard 需 follow-up。 | high |
| OUTCOME-003 | partial | core/session 通过；GUI view suite 因无 WindowServer/screens 未能运行到窗口创建。 | moderate |
| OUTCOME-004 | pass | CMake 和 WebViewFactory 通过平台分支/宏静态选择 backend，无动态加载或 registry。 | high |

| question | verdict | 直接答案 | evidence_refs | confidence | boundary | next_action |
|---|---|---|---|---|---|---|
| Does the public API require complete removal of concrete backend dependencies? | pass | Yes for public/demo/cross-platform paths: static scan found no Wk concrete casts or platform includes; the only cast remains inside the macOS backend popup implementation. | samples/demo/; tests/; src/webview/ static scan | high | The private macOS backend cast remains allowed. | Keep future backend code behind the public contract. |
| Does the contract express lifecycle and host-owned behavior? | partial | Mostly: public IWebView and WebViewTypes express initialization, attach/detach, navigation/bridge state consumption, popup ownership, host file/download callbacks, and explicit resource mappings; queued failure and completion guards need HOST-03. | src/webview/IWebView.h; src/webview/WebViewTypes.h; src/webview/WebViewState.cpp | high | HOST-03 remains open for asynchronous failure and completion safety. | Execute HOST-03 before REVIEW-02. |
| Did macOS navigation, popup, bridge, session, and close semantics regress? | partial | Core and macOS session suites pass (2/2); the full view suite aborts before window creation because no screens are available, so real GUI navigation/popup/bridge E2E is not claimed. | ctest core/session 2/2; webview_macos_view_tests: no screens | moderate | GUI-only behavior was not observed in this environment. | Run the view suite in a logged-in WindowServer session. |
| Is backend selection compile-time only? | pass | CMake source selection and WebViewFactory use platform branches/macros; static scan found no runtime registry, plugin loader, dlopen, or LoadLibrary path. | CMakeLists.txt; src/webview/WebViewFactory.cpp; static dynamic-path scan | high | Evidence is for the current macOS configuration. | Reuse the same compile-time boundary for Windows implementation. |

| 应用、demo和跨平台测试是否已经完全摆脱WkWebView/WkWebViewSession具体类型？ | pass | Yes for public/demo/cross-platform paths: static scan found no Wk concrete casts or platform includes; the only cast remains inside the macOS backend popup implementation. | samples/demo/; tests/; src/webview/ static scan | high | The private macOS backend cast remains allowed. | Keep future backend code behind the public contract. |
| 公共接口和状态机是否完整表达attach、初始化、navigation、bridge、popup及host-owned边界？ | partial | Mostly: public IWebView and WebViewTypes express initialization, attach/detach, navigation/bridge state consumption, popup ownership, host file/download callbacks, and explicit resource mappings; queued failure and completion guards need HOST-03. | src/webview/IWebView.h; src/webview/WebViewTypes.h; src/webview/WebViewState.cpp | high | HOST-03 remains open for asynchronous failure and completion safety. | Execute HOST-03 before REVIEW-02. |
| macOS现有navigation、popup、bridge、session和关闭语义是否在抽离后保持通过？ | partial | Core and macOS session suites pass (2/2); the full view suite aborts before window creation because no screens are available, so real GUI navigation/popup/bridge E2E is not claimed. | ctest core/session 2/2; webview_macos_view_tests: no screens | moderate | GUI-only behavior was not observed in this environment. | Run the view suite in a logged-in WindowServer session. |
| 构建是否仍只通过CMake和平台宏静态选择backend？ | pass | CMake source selection and WebViewFactory use platform branches/macros; static scan found no runtime registry, plugin loader, dlopen, or LoadLibrary path. | CMakeLists.txt; src/webview/WebViewFactory.cpp; static dynamic-path scan | high | Evidence is for the current macOS configuration. | Reuse the same compile-time boundary for Windows implementation. |

| Does the public API require complete removal of concrete backend dependencies? | pass | Yes for public/demo/cross-platform paths: static scan found no Wk concrete casts or platform includes; the only cast remains inside the macOS backend popup implementation. | samples/demo/; tests/; src/webview/ static scan | high | The private macOS backend cast remains allowed. | Keep future backend code behind the public contract. |
| Does the contract express lifecycle and host-owned behavior? | partial | Mostly: public IWebView and WebViewTypes express initialization, attach/detach, navigation/bridge state consumption, popup ownership, host file/download callbacks, and explicit resource mappings; queued failure and completion guards need HOST-03. | src/webview/IWebView.h; src/webview/WebViewTypes.h; src/webview/WebViewState.cpp | high | HOST-03 remains open for asynchronous failure and completion safety. | Execute HOST-03 before REVIEW-02. |
| Did macOS navigation, popup, bridge, session, and close semantics regress? | partial | Core and macOS session suites pass (2/2); the full view suite aborts before window creation because no screens are available, so real GUI navigation/popup/bridge E2E is not claimed. | ctest core/session 2/2; webview_macos_view_tests: no screens | moderate | GUI-only behavior was not observed in this environment. | Run the view suite in a logged-in WindowServer session. |
| Is backend selection compile-time only? | pass | CMake source selection and WebViewFactory use platform branches/macros; static scan found no runtime registry, plugin loader, dlopen, or LoadLibrary path. | CMakeLists.txt; src/webview/WebViewFactory.cpp; static dynamic-path scan | high | Evidence is for the current macOS configuration. | Reuse the same compile-time boundary for Windows implementation. |

## Decisive Result

公共契约已足以承载 macOS 生产路径并隔离业务代码，但发现一个当前范围的 host completion 安全缺口，已转成正式 follow-up。GUI-only 证据仍受环境限制，不升级为 real E2E 通过。

## 决定整体状态的结果

见 Decisive Result。

## Blocked Claims

## 目前仍不能声称什么

| claim | reason | release_condition |
|---|---|---|
| Windows WebView2 backend、vcpkg接入或Runtime发布已经完成。 | 这些内容是批准规范的明确Non-goal。 | 单独批准并执行Windows WebView2 backend任务，取得Windows集成证据。 |
| 所有平台底层API行为完全相同。 | 本任务统一公共可观察语义，不承诺原生API或Runtime能力完全一致。 | 各平台实现完成后逐项验证capability与行为映射。 |

## Validation And Next Action

`cmake --build build/debug -j 4` 成功；`webview_core_tests` 和 `webview_macos_session_tests` 通过。完整 ctest 为 2/3，`webview_macos_view_tests` 因没有 WindowServer/screens 中止。先完成 `HOST-03` 的 one-shot/close-safe completion guard，再执行 REVIEW-02；另外在登录的 macOS GUI 会话运行 `./build/debug/webview_macos_view_tests`。

## 验证情况与后续可操作

见 Validation And Next Action。

## 施工细节

host 在最终 Qt layout 激活后调用 `attachNativeView()`，移动或销毁前调用 `detachNativeView()`。popup 由 host 通过 `NewWindowDisposition` 和 `WebViewPtr&` 接管；file selection、download target 和 app resource mapping 都由公共回调或显式 options 提供。backend 不创建 `NSOpenPanel`、不猜下载目录，也不隐式推断 attach 时机。

## Spec 目标逐条对账

| 目标 | 状态 | 备注 |
|---|---|---|
| 公共契约替代具体 backend 依赖 | 完成 | 公共/demo/test 路径通过静态扫描；backend 私有 cast 保留。 |
| 生命周期和 host-owned 决策可观察 | 部分完成 | 接口已存在；HOST-03 补齐异步 completion 和 ready failure 语义。 |
| macOS 行为回归 | 受限 | core/session 通过，GUI 无屏幕。 |
| 编译期 backend 选择 | 完成 | 无 runtime registry/plugin loader。 |
