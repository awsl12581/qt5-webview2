---
mission: spec
status: approved
created: 2026-08-16
approved_at: 2026-08-16T13:33:03+08:00
---

# Windows WebView2 Follow-ups

## Goal

补齐 Windows WebView2 backend 在 session 生命周期、COM 线程约束、bridge 安全边界、运行时验证和部署说明上的缺口，使现有可运行 demo 具备可复现、可审计的交付标准。

## Scope

- 明确并实现 session 销毁对 root view、popup view、controller 和 pending completion 的关闭语义。
- 明确 WebView2 COM API、Qt UI 线程和跨线程 host completion 的调用边界。
- 验证并记录 WebMessageReceived 的 main-frame 语义；若 SDK 版本无法提供 frame 判断，定义并测试 Windows backend 的明确限制。
- 建立真实桌面 WebView2 Runtime harness，覆盖 controller、attach/detach/resize、navigation、bridge、app:// mapping、popup、download、permission 和 close race。
- 补充 persistent、ephemeral/InPrivate、website data 清理和 capability 的运行时验收。
- 补充 WebView2 SDK、Loader DLL、WebView2 Runtime、ARM64 Runtime 和 vcpkg DLL 自动部署之间的文档边界。
- 同步 Windows spec、architecture 文档、测试证据和受限验收记录。

## Non-goals

- 不实现 WebView2 Runtime 安装器、Evergreen Runtime 发布器或企业部署策略。
- 不实现 HTML file chooser shim；`FileSelection` 继续报告 `Unsupported`。
- 不修改公共 WebView API 以暴露 WebView2、COM 或 HWND 类型。
- 不改变 macOS backend 的既有行为。

## Design

Session close 必须使所有由该 session 创建的 view 进入 closed/inert 状态，并关闭 controller、注销事件、取消 popup/download 等 pending action。保留在 host 手中的 view 仍可安全析构和查询状态，但不得继续调用 WebView2。

所有 WebView2 COM 操作必须在创建环境的 Qt UI STA 线程执行。异步 host completion 可以来自任意线程，但在访问 deferral、controller 或 download args 前必须切回该线程。线程约束、关闭竞态和重复 completion 使用现有 scheduler、lifetime 和 completion guard 机制表达。

Bridge 验收必须覆盖当前 committed origin、document token、消息 schema、大小和 frame 语义。若 WebView2 SDK 版本没有可用的 main-frame 判断接口，必须在实现和文档中明确这一限制，不得把 origin 检查描述成 main-frame 校验。

Runtime harness 使用真实 HWND 和交互桌面 WebView2 Runtime。每个场景记录 Runtime 版本、架构、HRESULT、超时和退出码。缺少 Runtime、桌面会话或 GUI 能力时，只记录受限验收，不把静态检查或编译结果写成 E2E 通过。

## Acceptance Criteria

- session 销毁后，保留的 root/popup view 均为 closed/inert，controller 和 pending deferral/completion 不再生效。
- 跨线程调用违反线程契约时有明确行为；合法 worker-thread completion 能安全回到 Qt UI 线程并只完成一次。
- bridge 的 main-frame 能力结论与 SDK 实际接口一致；同源 iframe、跨 origin、旧 token 和导航后旧消息均有测试或明确限制记录。
- Debug/Release ARM64 harness 能在可用 Runtime 上验证 controller、attach/detach/resize、navigation、bridge、app:// 相对资源和 fetch、popup、download、permission、profile 清理及 close race。
- persistent 与 ephemeral profile 不静默降级；cache、cookies、website data 清理返回明确成功或 HRESULT 错误。
- `FileSelection` 保持 `Unsupported`，PrivateProfile/ResourceMapping capability 按 Runtime/interface 能力报告。
- 文档明确区分 WebView2 SDK、WebView2Loader.dll、Qt/vcpkg DLL 自动部署和 WebView2 Runtime 安装责任。
- 测试证据逐项区分静态检查、编译验证、Runtime session 验证和 GUI E2E；受限路径记录原因、未验证范围和手工命令。
