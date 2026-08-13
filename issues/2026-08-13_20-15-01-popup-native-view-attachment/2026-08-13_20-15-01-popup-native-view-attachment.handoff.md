# Popup Native View Attachment -- 施工交工单

> 独立性: true | mode=reviewer-subagent | requested_model=gpt-5.6-sol
> 日期: 2026-08-13

## 先看结论

整体判定为 partial。代码已让 popup 在 tab 布局完成后才请求原生挂载，核心和 session 测试通过；但当前进程没有可用屏幕，尚未运行真实 WKWebView popup 测试，也未人工确认闪烁消失。最重要的未完成结论是：不能声称所有 macOS 环境的 resize flash 都已消失。

## 这份交工单告诉你什么

它汇总批准规格、实现、测试和独立审查的结果，供后续验证和交接使用。它不替代 WindowServer 中的真实页面运行，也不证明尚未执行的视觉检查。

## 你现在可以确定什么

| 你关心的问题 | 判定 | 直接答案 | 关键证据 | 可信度 | 结论边界 | 下一步 |
| --- | --- | --- | --- | --- | --- | --- |
| Does a permitted popup enter QTabWidget before its WKWebView is attached to a native Qt host? | pass | The popup starts detached and the demo releases native attachment only after it inserts and lays out the tab. | tests/WkViewTests.mm:381; samples/demo/DemoWindow.cpp:100 | high | The real popup callback test requires WindowServer to execute. | Run webview_macos_view_tests in a logged-in GUI session. |
| Does first native attachment use a nonzero tab-host bounds while retaining WebKit's popup configuration and existing lifecycle behavior? | partial | The code and integration assertion retain the WebKit configuration and attach at 640x480, while core and session suites pass. | src/platform/macos/WkWebView.mm:469; tests/WkViewTests.mm:382; ctest core/session: 2/2 passed | moderate | The new view assertion was not executed without WindowServer. | Run webview_macos_view_tests in a logged-in GUI session. |
| Has the user-visible resize flash been checked in a WindowServer session? | not_run | The resize flash has not been visually checked in this environment. | ctest --preset default-debug: Cannot create window: no screens available | unknown | No screen is available to the test process. | Open system_webview_demo and use Open a new tab in a logged-in macOS session. |

## 决定整体状态的结果

代码路径已经将 native attachment 放到 tab 布局之后，但真实 WebKit popup 运行和视觉检查仍未执行，所以整体不能判定为完成。

## 仍不能声称什么

| 不能声称的结论 | 原因 | 解除条件 |
| --- | --- | --- |
| The visual resize flash is absent in every macOS environment. | It requires a real WindowServer session and is not established by headless or source-only checks. | Manually open the demo popup in a logged-in WindowServer session and inspect its first render. |

## 验证与下一步

在已登录的 macOS 桌面会话运行 `./build/debug/webview_macos_view_tests`，然后启动 `./build/debug/samples/demo/system_webview_demo` 并点击 `Open a new tab`。
