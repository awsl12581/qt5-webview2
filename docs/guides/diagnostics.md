# 诊断日志与崩溃信息接入

> 最后修改时间：2026-09-24 18:53 CST
> 文档版本：v2
> 修改说明：补充 WebView2 browser process 故障在 view 和 session 两个作用域的回调语义。

system_webview 只产生诊断信息，不创建日志或 dump 文件。初始化、导航和异步操作的业务结果仍通过公开 API 返回；宿主使用 Qt 日志设施决定过滤、落盘、轮转和上传方式。进程级 dump 由宿主或操作系统负责。

## 数据流

```text
公共逻辑 / WKWebView / WebView2
    |-- 业务结果 ------> InitializationResult、LoadEvent、completion
    |-- 运行时故障 ----> WebViewHostCallbacks / WebViewSessionHostCallbacks
    `-- 诊断事件 ------> QLoggingCategory ------> 宿主 Qt message handler

宿主进程或浏览器进程崩溃 ----------------------> WER、Crashpad 或 macOS crash report
```

日志调用在事件发生线程同步执行。库不维护日志队列；宿主的 message handler 不应执行阻塞 I/O。

## 日志分类

| 分类 | 内容 |
| --- | --- |
| `system_webview.lifecycle` | session 和 view 的创建、初始化及关闭 |
| `system_webview.navigation` | 导航失败 |
| `system_webview.policy` | popup、下载和权限拒绝 |
| `system_webview.bridge` | Bridge 边界校验、协议校验和传输失败 |
| `system_webview.resource` | 发布资源、快照和读取失败 |
| `system_webview.runtime` | WebContent、renderer、browser 和辅助进程故障 |

Debug 事件默认关闭，Info、Warning 和 Critical 默认开启。开发环境可以通过环境变量启用完整诊断：

```sh
QT_LOGGING_RULES="system_webview.*.debug=true"
QT_MESSAGE_PATTERN="%{time process} %{type} %{category} %{message}"
```

应用需要持久化日志时，应在宿主入口安装 `qInstallMessageHandler`，再将消息交给应用现有的日志设施。库不规定文件格式、目录、大小或保留时间。

## 公共字段

每条消息以稳定的 `event=<事件码>` 开头。可能附带以下字段：

| 字段 | 说明 |
| --- | --- |
| `session` | 当前进程内单调递增的 session 关联 ID |
| `view` | 当前进程内单调递增的 view 关联 ID |
| `navigation` | 原生导航 ID 或公共导航 ID |
| `origin` | 只保留 scheme、host 和显式 port 的来源 |
| `status` | HTTP 或资源响应状态 |
| `reason` | 不含页面数据的固定原因码 |
| `native_code` | 原生枚举或错误码 |
| `process` | 操作系统进程 ID；只用于 session 级 Runtime 故障 |

关联 ID 只在当前进程生命周期内有效，不是持久标识。

## 事件码

| 分类 | 级别 | 事件码 | 触发条件 |
| --- | --- | --- | --- |
| lifecycle | Debug | `session.created` | session 状态创建 |
| lifecycle | Info | `session.ready` | session 初始化成功 |
| lifecycle | Warning | `session.initialization_failed` | session 初始化失败 |
| lifecycle | Debug | `session.closed` | session 关闭 |
| lifecycle | Debug | `view.created` | view 状态创建 |
| lifecycle | Info | `view.ready` | view 初始化成功 |
| lifecycle | Warning | `view.initialization_failed` | view 初始化失败 |
| lifecycle | Debug | `view.closed` | view 关闭 |
| navigation | Debug | `navigation.rejected` | policy 拒绝或转交外部处理 |
| navigation | Warning | `navigation.failed` | 已开始的主框架导航失败 |
| policy | Debug | `policy.popup_denied` | popup 被 policy 或缺失宿主回调拒绝 |
| policy | Debug | `policy.permission_denied` | 媒体或其他权限未获允许 |
| policy | Debug | `policy.download_denied` | 下载被 policy 或缺失宿主回调拒绝 |
| bridge | Debug | `bridge.message_rejected` | 原生边界、协议、schema、token、origin 或文档状态校验失败 |
| bridge | Debug | `bridge.transport_unavailable` | 页面 transport 尚未建立 |
| bridge | Warning | `bridge.transport_failed` | 原生 transport 拒绝发送消息 |
| resource | Debug | `resource.publish_rejected` | 文件状态、文档身份或容量限制不允许发布 |
| resource | Warning | `resource.snapshot_failed` | 创建或写入不可变快照失败 |
| resource | Debug | `resource.request_rejected` | 资源请求返回 403、404、410 或 416 |
| resource | Warning | `resource.read_failed` | 已发布快照无法打开或定位 |
| runtime | Critical | `runtime.web_content_terminated` | WKWebView 内容进程终止 |
| runtime | Critical | `runtime.process_failed` | WebView2 `ProcessFailed` 事件 |
| runtime | Critical | `runtime.browser_process_terminated` | WebView2 browser process 异常退出 |

事件码和字段名是诊断契约。描述文本供人阅读，不应用于程序分支判断。

## 运行时故障回调

`WebViewHostCallbacks::onRuntimeFailure` 接收 WebView2 `ProcessFailed` 或 WKWebView 内容进程终止事件。`WebViewSessionHostCallbacks::onRuntimeFailure` 接收 WebView2 环境报告的 browser process 异常退出。同一次 WebView2 browser process 故障可能在受影响的 view 和 session 两个作用域各报告一次；宿主应按作用域处理。关闭后的对象不再触发回调。

`RuntimeFailureEvent::nativeCode` 在 Windows 上保存 WebView2 原生枚举或进程 ID；macOS 无对应数值时为 `0`。宿主应根据 `RuntimeFailureKind` 决定重建 view 或 session，不要解析 `reason` 文本。

## 脱敏边界

库日志允许记录来源、关联 ID、状态码、文件大小和原生错误码。以下内容不得写入日志：

- URL userinfo、path、query 和 fragment；
- Cookie、HTML、Bridge payload、request ID 和 document token；
- profile、本地资源、下载目标和用户选择文件的绝对路径；
- 页面 `console.log` 内容。

业务回调可能包含完整 URL 或错误文本。宿主将这些值写入自己的日志前仍需执行自身的隐私策略。

## Dump 与符号

库不安装异常处理器，也不搜索 WebView Runtime 的 dump 目录。Windows 宿主可选择 WER LocalDumps 或 Crashpad；macOS 宿主使用系统 crash report 或已有崩溃 SDK。发布流程必须保存与二进制版本匹配的 PDB 或 dSYM，否则 dump 无法可靠符号化。

WebView 子进程故障首先通过 runtime failure 回调报告。操作系统或 WebView Runtime 是否生成 dump、dump 保存在哪里，取决于目标机器配置，不属于本库保证。

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-09-24 18:29 CST | 新增诊断框架、事件码、脱敏规则和 dump 接入边界。 |
| v2 | 2026-09-24 18:53 CST | 补充 WebView2 browser process 故障的双作用域回调语义。 |
