# Web application API 施工交工单

WARNING: self-review only, NOT independently verified
独立性: false（当前 sandbox 禁止独立 Codex review 写入状态数据库）

这轮把公开加载模型从资源映射改成应用。公共 API、LocalBundle、安全 resolver、Demo、文档和安装 consumer 已完成；跨平台 GUI 资源加载还没有在可交互桌面上验证，因此整体结论是部分完成。

## 先看结论

代码和可在当前主机运行的验证都通过，但 native GUI 资源加载缺少交互桌面证据，所以本次不能给出跨平台 E2E pass。

## 这份交工单告诉你什么

它汇总 approved spec、提交记录、测试、安装 consumer 和本轮自审证据。它不把已编译的代码说成已完成的跨平台 GUI E2E。

## 现在可以确定什么

| 你关心的问题 | 判定 | 直接答案 | 关键证据 | 可信度 | 结论边界 | 下一步 |
|---|---|---|---|---|---|---|
| Can an application use the same public lifecycle to open a Vite dist bundle, a Vite development server, and an HTTPS origin? | partial | The installed API compiles for LocalBundle, DevelopmentServer, and RemoteOrigin; both backend source paths use open(application, route). | tests/JsonMessageTests.cpp; src/platform/macos/WkWebView.mm; src/platform/windows/WebView2View.cpp; /private/tmp/system-webview-consumer/build4 | moderate | Windows MSVC/Runtime and native GUI navigation were unavailable. | Run Windows ARM64 preset and GUI runtime probe. |
| Are local bundle resources secure and does SPA fallback avoid masking missing static or fetch resources? | partial | Core tests cover root validation, traversal and symlink rejection, document-only SPA fallback, and missing static resources. | tests/JsonMessageTests.cpp; src/webview/ResourceMapping.cpp | moderate | Live WebKit/WebView2 resource requests were not runnable. | Run macOS view tests in a logged-in session and the Windows runtime probe. |
| Has the old resource mapping concept been fully removed from public developer-facing code without a compatibility path? | pass | The old public mapping type, option, capability, and caller entry points are removed; an external consumer builds against the new API. | src/webview/WebViewTypes.h; /private/tmp/system-webview-install/include; /private/tmp/system-webview-consumer/build4 | high | Historical specs and ADRs retain old names as decision records. | No implementation action required. |

## 决定整体状态的结果

三种 source 共用 `open(application, route)`，但真实 native GUI 请求没有可用桌面环境验证。代码完成不等于跨平台 GUI E2E 已证明，因此这里保留 partial 结论。

## 目前仍不能声称什么

| 不能声称的结论 | 原因 | 解除条件 |
|---|---|---|
| A backend without the required native resource-serving capability supports LocalBundle. | The approved spec requires an explicit failure rather than an implicit HTTP-server fallback. | The backend implements the capability and passes equivalent integration tests. |

## 施工细节

```text
改之前：应用配置 app:// origin 和目录 → view 直接 load URL
改之后：session 创建 WebApplication → view.open(application, route) → 内部 resolver 提供 bundle
```

LocalBundle 继续拒绝路径穿越、软链接逃逸、目录和缺失文件。SPA fallback 仅对 document 请求生效，丢失的 JS、CSS 和 fetch 不会返回入口页。Vite development server 需要加入 `trustedDevelopmentOrigins`，不会放开任意 HTTP origin。

## 验证情况与下一步

通过：macOS debug build、core/session tests、Windows static contract check、安装 package consumer 的 configure/build。

受限：`webview_macos_view_tests` 因无 screen 无法创建窗口；Windows ARM64 MSVC 与 WebView2 Runtime 不在当前宿主可用范围。

## 后续可操作

在有图形桌面的 macOS 会话运行：

```sh
ctest --test-dir build/macos-appleclang-debug --output-on-failure -R webview_macos_view_tests
```

在 ARM64 Windows 的 VS developer prompt 中运行：

```bat
cmake --preset windows-msvc-arm64-debug
cmake --build --preset windows-msvc-arm64-debug
ctest --preset windows-msvc-arm64-debug
```
