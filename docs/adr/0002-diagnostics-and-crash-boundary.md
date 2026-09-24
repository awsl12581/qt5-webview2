---
id: "0002"
title: "以 Qt 分类日志记录诊断并由宿主负责持久化和 dump"
status: "accepted"
date: "2026-09-24"
recorded: "2026-09-24"
supersedes: null
superseded-by: null
tags: ["diagnostics", "logging", "crash-reporting"]
aliases: ["logger", "dump", "minidump", "RuntimeFailureEvent"]
paths: ["include/system_webview", "src/webview", "src/platform", "docs/guides"]
---

# 0002. 以 Qt 分类日志记录诊断并由宿主负责持久化和 dump

> 最后修改时间：2026-09-24 18:47 CST
> 文档版本：v2
> 修改说明：修正 macOS 测试结果，并准确记录 Windows 验证边界。

## Context

system_webview 是嵌入宿主进程的 Qt 动态库。初始化、导航和异步操作错误已有类型化返回值，但内部拒绝路径、浏览器子进程故障和原生 API 失败缺少统一诊断。库需要提供跨平台且可关联的日志，同时不能接管宿主已有的日志文件、上传或进程级崩溃设施。

## Decision

库通过 QLoggingCategory 发出带稳定事件码和 session/view/navigation 关联 ID 的诊断日志；业务错误继续使用现有类型化结果。WebView 运行时故障通过公共回调报告。宿主控制日志过滤、落盘、轮转和上传，并负责 WER、Crashpad 或 macOS crash report 等 dump 设施。日志不记录 payload、token、HTML、URL 查询参数或本地绝对路径。

## Rejected

- 自建 Logger、文件 sink 和轮转线程：会重复宿主已有设施，并把存储策略强加给所有调用方。
- 只使用业务错误回调：无法覆盖被安全边界拒绝的输入和浏览器子进程异常，也不适合调试内部状态。
- 在库内安装全局异常处理器或 Crashpad：动态库不能安全接管宿主进程的崩溃策略，还会与应用已有处理器冲突。

## Consequences

调用方可通过 QT_LOGGING_RULES 和 Qt message handler 接入自己的日志系统。Debug 诊断默认关闭，Info 以上默认可见。公共 runtime failure 类型和 session callback 构成 0.2.0 ABI 变更，使用方必须重新编译。库不生成 log 或 dump 文件；dump 的保留、符号化和上传需要宿主保存匹配版本的 PDB 或 dSYM。

## Acceptance Evidence - 2026-09-24

- `webview_core_tests` 验证了分类日志、稳定事件码、关联 ID、URL 脱敏和关闭后的故障回调抑制。
- macOS Debug 构建通过；`webview_core_tests` 和 `webview_macos_session_tests` 通过。`webview_macos_view_tests` 因运行环境没有可用屏幕，在创建窗口前失败，未进入断言验证。
- Windows 静态契约脚本已执行，但被仓库原有的 `samples/demo/DemoWindow.cpp` 中 `HWND` 和 `tests/WebView2ContractTests.cpp` 中 `windows.h` 两处检查项阻断；本次诊断改动未修改这两个文件。本次未在 Windows 上编译，也未执行 WebView2 Runtime 或 GUI E2E；真实 Runtime 行为仍按平台矩阵中的证据边界解释。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-09-24 18:29 CST | 记录诊断日志、运行时故障回调和 dump 的职责边界。 |
| v2 | 2026-09-24 18:47 CST | 修正 macOS 测试结果，并准确记录 Windows 静态检查、编译和运行时验证边界。 |
