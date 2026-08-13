---
mission: spec
status: approved
created: 2026-08-13
approved_at: 2026-08-13T20:15:01+08:00
---

# Popup Native View Attachment

## Goal

消除 demo 在新 tab 打开时，WKWebView 先按临时尺寸绘制、随后才适配 tab 内容区的闪烁。

## Scope

- 调整 macOS `WkWebView` 的原生视图挂载时机。
- 保留 `WKUIDelegate` popup 回调同步返回使用 WebKit 提供的 configuration 创建的 `WKWebView`。
- 让 demo 在子页面插入 `QTabWidget` 并完成布局后，再将其 WKWebView 挂入对应的原生 Qt 宿主。
- 添加覆盖 popup 挂载时序和最终几何尺寸的 macOS 回归测试；可观察的 UI 闪烁需要在 WindowServer 会话中人工确认。

## Design

`WkWebView` 将其 WKWebView 的创建和原生嵌入分开。构造阶段创建 WKWebView、配置代理并保留它，但不会对无父级 `NativeViewHost` 调用 `winId()`，也不会调用 `addSubview:`。

`IWebView::widget()` 返回可插入 Qt 布局的宿主 widget。popup host 回调可同步将该 widget 放入 tab，激活布局，然后请求后端附加原生视图。附加时取得已在 Qt 层级中的 native host，使用其最终 bounds 设置 WKWebView frame，并将 WKWebView 加入该 NSView。

普通视图也走同一附加路径。关闭尚未附加或已经附加的 view 都必须保持幂等，并继续清理 WebKit 代理、脚本消息处理器和 session 注册。

## Acceptance Criteria

- `WKUIDelegate` popup 回调返回的 WKWebView 使用 WebKit 提供的 configuration 创建。
- popup 在 host 的 `newWindow` 回调返回前，仍能同步返回 WKWebView 给 WebKit。
- `NativeViewHost` 在加入 Qt 父级前不创建 native handle，也不接收 WKWebView 子视图。
- 子 tab 布局完成后，WKWebView 首次附加时的 frame 等于宿主的非零 bounds。
- 新 tab 不出现由初始 `1x1` 或无父级 native host 重挂载导致的尺寸闪烁。
- 初始 tab、popup tab、关闭 tab 与 session 销毁仍通过现有生命周期和回调测试。
- `system_webview_demo`、`webview_macos_session_tests` 和 `webview_macos_view_tests` 构建成功；后者在有 WindowServer 的 macOS 会话中通过。

## Non-goals

- 不引入 `QWindow`、`createWindowContainer` 或改用 Qt WebEngine。
- 不改变 popup、导航、桥接、权限或 session 的公共 API 和安全策略。
- 不保证页面网络加载本身不会重绘；本任务只处理原生视图挂载造成的尺寸闪烁。

## Alternatives

在现有挂载顺序上临时隐藏 WKWebView 被拒绝。它只掩盖首帧，不能消除无父级 native host 创建后再重挂载的根因。

在 demo 中调用 `resize()`、`processEvents()` 或强制布局也不采用。它依赖事件循环时序，不能保证 WKWebView 的原生首次绘制发生在最终 geometry 上。

## Compatibility

公共 C++ 接口保持不变。改动仅限 macOS 后端及 demo 对内部附加时机的协调。

## Rollback

若延迟附加导致 WebKit popup 请求无法继续加载，恢复当前立即附加路径，并保留新增测试以定位需要在 callback 中完成的最小准备步骤。
