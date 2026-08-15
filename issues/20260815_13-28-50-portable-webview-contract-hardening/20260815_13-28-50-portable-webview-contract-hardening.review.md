# Portable WebView Contract Hardening Review

- 独立 reviewer：`reviewer-subagent`
- 独立性：`true`
- 请求模型：`gpt-5.6-sol`
- 观测模型：`unknown`，当前 agent metadata 未提供可核验模型名
- 结论：`limited_review`
- claims：`16/16` 已逐条检查

## 结论

批准范围内的公共契约和 macOS 接线已完成，没有发现仍需追加 issue 的 current-scope gap。构建、core/session 测试和静态边界扫描通过。`webview_macos_view_tests` 因当前环境没有 WindowServer/screens，在创建窗口前中止，因此 file/download delegate、popup、bridge、resource mapping 和 geometry 的真实 GUI E2E 仍未观察。

## 定向复核

reviewer 初次提出初始化队列和 file completion 两个疑点。回读 HEAD 后确认二者均已解决：`InitializationScheduler` 会将 Failed/Closed 传给队列，macOS attach/load/setHtml 均通过 `runWhenReady`；file selection 使用 `HostCompletionGuard`、weak state、`QPointer` 和 queued UI dispatch。两项不构成 gap，也不进入 deferred ledger。

## 证据边界

- `cmake --build build/debug -j 4` 成功。
- `webview_core_tests`、`webview_macos_session_tests` 通过。
- 完整 `ctest` 为 2/3；view test 输出 `Cannot create window: no screens available`。
- 未发现旧同步 download resolver、旧 popup disposition、`NSOpenPanel`、runtime registry、`dlopen` 或 `LoadLibrary`。

## 分类

- current-scope gaps：无。
- deferred findings：无。
- human-required blocker：需要在已登录且有屏幕的 macOS WindowServer 会话运行 view suite。
