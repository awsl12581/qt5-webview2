# WebView Session 与安全策略重构 - 施工交工单

> 独立性: false | mode=self-review | requested_model=gpt-5.6-sol
> 日期: 2026-08-12

## 先看结论

这轮重构的主体已经落地：应用必须先创建 session，再从 session 创建页面；macOS 的登录态共享、无痕存储、加载事件、导航拦截、popup、bridge 来源校验和关闭流程都有实现与测试。但现在还不能判定整体通过。第一处明确缺口是 native 发往网页的消息仍会拼进 JavaScript 源码；另一个缺口是权限与下载回调没有经过统一 policy。WebView2 和 WebKit2GTK 也仍然只是接口映射设计，并未实现。

## 这份交工单告诉你什么

这份交工单把批准规范、代码、自动测试、真实 WKWebView 测试和提交记录放在一起说明。它供接口维护者和后端实现者判断当前进度，不会把“代码写完”说成“所有平台能力都已验证”，也不会替代以后两套后端自己的平台测试。

## 你现在可以确定什么

| 你关心的问题 | 判定 | 直接答案 | 关键证据 | 可信度 | 结论边界 | 下一步 |
|---|---|---|---|---|---|---|
| Does the public API require explicit sessions and fully remove the superseded standalone and unrestricted bridge APIs? | pass | The public API requires explicit sessions, and the superseded standalone factory and direct bridge/popup setters are absent from production headers and consumers. | src/webview/WebViewFactory.h:9; src/webview/IWebView.h:12; samples/demo/DemoWindow.cpp:77 | high | This is a source-level contract result and does not provide a compatibility layer for old callers. | None |
| Do macOS views in one session share profile resources while ephemeral sessions use non-persistent storage? | pass | Views from one macOS session share the website data store and assigned process-pool identity, while ephemeral sessions use non-persistent storage. | src/platform/macos/WkWebViewSession.mm:10; tests/WkSessionTests.mm:31; ctest webview_macos_session_tests passed | high | profilePath is not a custom WebKit storage directory, and WKProcessPool does not add isolation on modern macOS. | None |
| Are lifecycle, navigation, popup, and close semantics observable and enforced without stale callbacks? | pass | macOS exposes ordered lifecycle events, policy-controlled navigation and popups, stable navigation IDs, and idempotent close with stale callback suppression. | src/platform/macos/WkWebView.mm:69; tests/WkViewTests.mm:49; ctest webview_macos_view_tests passed | high | The real page suite needs a logged-in macOS WindowServer session. | None |
| Can only a trusted current main frame exchange size-, version-, type-, and schema-validated bridge messages? | partial | Inbound bridge messages are restricted to the trusted current main frame and validated by size, version, type, and schema, but outbound payloads are still inserted into JavaScript source after JSON serialization. | src/platform/macos/WkWebView.mm:111; src/platform/macos/WkWebView.mm:435; tests/JsonMessageTests.cpp:10 | high | The approved no-source-interpolation guarantee is not met until FOLLOWUP-01 is complete. | Complete FOLLOWUP-01 and rerun the hostile-payload WKWebView integration test. |
| Is the common contract implementable on WKWebView, WebView2, and WebKit2GTK without exposing native profile types? | pass | The common headers contain no native profile types and the architecture maps the contract to WKWebView, WebView2, and WebKit2GTK; only macOS is implemented. | src/webview/WebViewTypes.h:1; docs/ARCHITECTURE.md:54; docs/ARCHITECTURE.md:62 | moderate | Portability is a reviewed design claim, not production evidence for the two unimplemented backends. | Validate each mapping when its backend is implemented. |

## 决定整体状态的结果

成功条件是 macOS 生产路径完整使用 session 和 policy 合同，并有足够证据支撑生命周期与 bridge 隔离。当前已经跑通加载、重定向、失败、popup 拒绝、iframe 拒绝、session 共享和无痕存储。整体仍是 `partial`：代码实现接近完成，但两项安全合同尚未满足，不能把它们合并成通过。

## 目前仍不能声称什么

| 不能声称的结论 | 原因 | 解除条件 |
|---|---|---|
| WebView2 and WebKit2GTK backends are implemented or production-tested. | They are explicit non-goals of this mission. | Implement each backend and run its platform integration suite. |
| The library provides a universal download manager. | The mission defines policy semantics but excludes a universal download manager. | Approve and implement a separate download API mission. |
| Profiles have stronger process isolation than each native backend provides. | The specification explicitly limits this guarantee to platform capabilities. | Document and implement backend-specific isolation capabilities where supported. |

## spec 目标逐条对账

| spec 目标 | 状态 | 实际效果 | 备注 |
|---|---|---|---|
| Session 统一管理登录态与网站数据 | 完成 | 同一 session 的 tab 共享 WebKit 数据存储，无痕 session 使用非持久存储。 | macOS 的 profilePath 是逻辑标识。 |
| 页面生命周期与关闭流程可观察 | 完成 | 宿主能收到稳定导航编号的加载事件，关闭后不会把旧回调算到新页面。 | 真实页面测试需要 WindowServer。 |
| 导航、popup 与 bridge 集中受控 | 部分完成 | 入站消息和页面导航已经经过策略；权限、下载和出站消息还有两处缺口。 | 后续 issue 会在本轮继续修。 |
| 三套后端共用一套业务接口 | 完成 | 公共头文件没有原生 profile 类型，并给出三套平台映射。 | 只实现了 macOS。 |

## 施工细节

页面创建路径已经变为 `创建 session -> session 创建 view -> host 持有 view -> tab 关闭时先 close`。macOS session 负责生成 configuration，同一 session 共享网站数据，单个页面仍有自己的 content controller 和 delegate。bridge 入站会核对主框架、当前 committed origin、消息版本、类型、大小和字段。

本轮发现的薄弱点不在主结构，而在两个容易被“默认拒绝很安全”掩盖的细节：默认拒绝权限并不等于请求经过了 policy；JSON 安全序列化也不等于没有把数据放进 JavaScript 源码。这两项会继续修复并重新审查。

## 验证情况与下一步

clean build、核心单测、session 集成测试均通过。真实 WKWebView 页面测试也通过，覆盖本地重定向、连接失败、popup 拒绝、iframe bridge 拒绝和重复关闭；该测试需要 macOS WindowServer，因此是在受限沙箱外运行。下一步是完成两条 follow-up，再运行第二轮愿景审查。
