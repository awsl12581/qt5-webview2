# WebView Session 与安全策略重构 - 施工交工单

> 独立性: true | mode=reviewer-subagent | requested_model=gpt-5.6-sol
> 日期: 2026-08-12

## 先看结论

第二轮审查确认，出站消息已经改成原生参数传递，权限和下载也接入了统一 policy。不过整体仍是 `partial`：旧页面的同源 bridge 消息还没有文档 token，redirect 没有传给 policy，session 销毁也不会主动关闭外部仍持有的 view。另有几条原生路径需要留下更完整的集成测试记录。

## 这份交工单告诉你什么

这份交工单面向接口维护者和后端实现者，依据批准规范、代码、测试、提交记录和独立审查结果说明当前状态。它不把设计映射当成另外两套后端已经可用，也不把静态接线检查写成真实系统交互已经通过。

## 你现在可以确定什么

| 你关心的问题 | 判定 | 直接答案 | 关键证据 | 可信度 | 结论边界 | 下一步 |
|---|---|---|---|---|---|---|
| Does the public API require explicit sessions and fully remove the superseded standalone and unrestricted bridge APIs? | pass | Yes. Public creation is session-only, and the former standalone factory plus unrestricted page bridge/popup APIs are absent from public C++, demo, tests, and documentation. | src/webview/WebViewFactory.h:10; src/webview/IWebViewSession.h:18; samples/demo/DemoWindow.cpp:77 | high | This is intentionally source-breaking; no compatibility shim is provided. | None |
| Do macOS views in one session share profile resources while ephemeral sessions use non-persistent storage? | pass | Yes. Configurations from one macOS session share its website data store and process pool, receive distinct content controllers, and ephemeral sessions use a non-persistent store. | src/platform/macos/WkWebViewSession.mm:19; tests/WkSessionTests.mm:39 | high | profilePath is logical on macOS; arbitrary storage placement and stronger process isolation are not promised. | None |
| Are lifecycle, navigation, popup, and close semantics observable and enforced without stale callbacks? | partial | Lifecycle IDs, failures, policy rejection, popup rejection, outbound generation checks, and idempotent close exist, but redirect policy context and session-bound callback lifetime are incomplete. | src/platform/macos/WkWebView.mm:236; tests/WkViewTests.mm:115 | high | Allowed popup inheritance, stop/reload, native capability callbacks, and overlapping callback races need stronger integration evidence. | Complete FOLLOWUP-04 through FOLLOWUP-06. |
| Can only a trusted current main frame exchange size-, version-, type-, and schema-validated bridge messages? | partial | Origin, main-frame, envelope, size/version/type/schema, outbound argument binding, and hostile-payload behavior are enforced, but inbound same-origin stale documents are not tied to the active document token. | src/platform/macos/WkWebView.mm:148; src/platform/macos/WkWebView.mm:514; tests/WkViewTests.mm:149 | high | The hostile test proves outbound data-only delivery, not stale inbound document rejection. | Complete FOLLOWUP-03 and add the same-origin stale-token test. |
| Is the common contract implementable on WKWebView, WebView2, and WebKit2GTK without exposing native profile types? | pass | At design-contract level, yes. Public headers expose portable Qt/C++ types and documentation maps the concepts to WKWebView, WebView2, and WebKit2GTK without native profile types. | src/webview/IWebViewSession.h:1; src/webview/WebViewTypes.h:1; docs/ARCHITECTURE.md:54 | moderate | Only macOS is implemented or production-tested. | Validate each future backend when implemented. |

## 决定整体状态的结果

主架构已经接通，两条上一轮安全缺口也已修复。现在决定整体状态的第一处失败，是入站 bridge 仍只比较 origin，没有比较文档 token；同源跳转时，旧文档排队中的消息还缺少可验证的代际边界。因此实现状态是“主体完成”，验证状态是“部分通过”，能力结论仍是 `partial`。

## 目前仍不能声称什么

| 不能声称的结论 | 原因 | 解除条件 |
|---|---|---|
| WebView2 and WebKit2GTK backends are implemented or production-tested. | They are explicit non-goals of this mission. | Implement each backend and run its platform integration suite. |
| The library provides a universal download manager. | The mission defines policy semantics but excludes a universal download manager. | Approve and implement a separate download API mission. |
| Profiles have stronger process isolation than each native backend provides. | The specification explicitly limits this guarantee to platform capabilities. | Document and implement backend-specific isolation capabilities where supported. |

## spec 目标逐条对账

| spec 目标 | 状态 | 实际效果 | 备注 |
|---|---|---|---|
| Session 管理登录态和网站数据 | 部分完成 | tab 已共享数据存储，无痕存储也可用。 | session 析构还要使外部持有的 view 失效。 |
| 页面生命周期和导航策略 | 部分完成 | 加载事件、失败和 popup 已可控。 | redirect 上下文仍未交给 policy。 |
| Bridge 只属于当前可信文档 | 部分完成 | origin、主框架、schema 和出站参数绑定已完成。 | 入站还缺文档 token。 |
| 三套后端使用统一业务接口 | 完成 | 公共接口不暴露原生类型，并有三套映射。 | 只有 macOS 已实现。 |

## 施工细节

上一轮的两个缺口已经有实质修复：native 发往页面的数据不再拼接到 JavaScript 源码，media、文件选择和下载入口也都先询问 policy。独立审查随后把注意力放到更细的生命周期边界，发现同源页面之间仍要用 token 区分，session 与 view 的寿命也必须真正绑定。

## 验证情况与下一步

clean build、核心测试、session 测试和真实 WKWebView 页面测试均通过，但最终证据还要补上 stop/reload、允许 popup 的 session 继承、native capability 回调和 race 场景，并把测试输出作为 mission 工件提交。下一步按 `FOLLOWUP-03..06` 继续，完成后进行第三轮审查。
