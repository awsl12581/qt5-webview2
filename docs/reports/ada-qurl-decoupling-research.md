# Ada 替代 QUrl 方案

> 最后修改时间：2026-10-25 23:24 CST
> 文档版本：v4
> 修改说明：记录 Ada URL 替代的落地范围、验证结果和实际语义调整。

## 结论

本项目使用 QUrl 的部分主要处理网址：解析、来源比较、路由、资源 URL 和平台回调中的 URL。Ada 能覆盖这些 URL 操作，包括 WHATWG 解析、规范化、相对 URL 解析和 `file://` URL。未发现阻碍替换的核心网址能力缺口。

文件系统访问和原生平台接口不是 URL 解析。Ada 不负责把文件 URL 映射成各平台可访问的文件路径，也不负责文件权限、目录校验或浏览器 API；这些由项目的文件系统逻辑和平台适配层处理。Ada 的 `href_from_file()` 也不完全符合本项目所有文件名的编码需求，因此不直接用它替代平台路径转换。

迁移目标是采用 ada/WHATWG 的网址规则，不复刻 QUrl 的旧结果。规则变化记入 API 文档，更新受影响的测试即可。

## 当前使用范围

- 公共 API 在导航、加载、策略、下载、文件选择、资源和宿主回调中传递 QUrl，见 [system_webview.h](../../include/system_webview/system_webview.h)。
- 核心代码用 QUrl 处理应用来源、白名单、资源路由、来源比较和诊断，主要位于 [Application.cpp](../../src/webview/Application.cpp)、[WebViewPolicy.cpp](../../src/webview/WebViewPolicy.cpp)、[ResourceMapping.cpp](../../src/webview/ResourceMapping.cpp) 和 [WebResourceManager.cpp](../../src/webview/WebResourceManager.cpp)。
- Windows 与 macOS 后端在系统 WebView URL 和项目 URL 之间转换，见 [WebView2View.cpp](../../src/platform/windows/WebView2View.cpp)、[WkWebView.mm](../../src/platform/macos/WkWebView.mm) 和 [WkWebViewSession.mm](../../src/platform/macos/WkWebViewSession.mm)。
- file URL 的路径访问、目录边界和链接检查属于文件系统适配，不交给 URL 库完成。

## 目标规则

- 由 Ada 解析和序列化 URL，采用 WHATWG 规范化结果，包括默认端口、网络域名 IDNA 和路径点段处理。测试和文档按新结果更新。
- `app://` 是项目自定义 scheme。Ada 返回的 `get_origin()` 为 `"null"`，所以应用身份使用 Ada 解析出的规范 host 构造项目键，不用 `get_origin()`。Opaque host 按 Ada 规则保留大小写；Unicode host 使用 Ada 的序列化形式。注册、白名单和资源映射使用同一键。
- route 按业务路径设置，不把 route 当作任意 URL 解析。页面资源从规范化 URL 取得路径，再由文件系统适配层解析；继续检查实际路径是否位于 bundle 根目录内。
- file URL 由 Ada 解析。需要转换成本地路径时使用平台适配，并验证 POSIX、Windows 盘符和 UNC 路径。
- Ada 隐藏在项目 URL 类型后，避免公共 API 直接暴露第三方内部类型。URL 相关公共字段统一迁移到项目类型，并记录源码与 ABI 变化。

## 实施与验收

迁移核心 URL 类型、来源比较、route、资源映射和两个平台的转换；同步更新样例、API 文档与测试。无需实现旧 QUrl 兼容分支。

验证 Ada 规范化网址、app 来源键、route 和本地 bundle 路径；再运行项目现有测试，确认导航、Bridge 来源校验、资源加载和文件访问在 macOS、Windows 均正常。文件 URL 转换按平台测试。以端到端功能正常作为验收标准，不扩展为逐项复刻 Qt 细节。

## 实施结果

公开 API、核心逻辑、macOS/Windows 后端、demo 和测试均已改用 `webview::Url`；源码与样例中不再引用 QUrl。Ada URL 类型提供本地文件转换、来源、相对解析和路由路径操作。测试按 WHATWG 规范化更新；`app:` host 大小写按 Ada 语义区分。macOS preset `macos-appleclang-verify` 构建成功，3 项测试通过。Windows 后端已完成源码迁移，未在本次 macOS 环境编译。

## 调研记录

基线为项目提交 `1ca001162b12a5e321dd2c283411c693cb1115fc` 和 ada 4.0.0 子模块。小型探针观察了 34 个 URL 输入、5 个 route、3 个相对 URL 和 4 个文件路径转换；运行环境为 macOS arm64、Apple Clang 21、Qt 5.15.19。探针不是完整平台集成测试。

具体配置、命令、原始输出和分析见 [实验记录](../../records/20261025-223240-ada-qurl-semantics/analysis.md)。本次审计的 QUrl 文件清单见 [scope-audit.json](../../records/20261025-223240-ada-qurl-semantics/scope-audit.json)。

## 参考

- [Ada README](../../3rdparty/ada/README.md)
- [Ada URL API](../../3rdparty/ada/include/ada/implementation.h)
- [WHATWG URL Standard](https://url.spec.whatwg.org/)

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 2026-10-25 22:35 CST | 审计 QUrl 依赖，以锁定 ada 4.0.0 的小型实验核验差异，并提出分阶段解耦与验收方案。 |
| v2 | 2026-10-25 22:40 CST | 将迁移原则调整为以 ada/WHATWG 为目标语义，允许有意变更旧 QUrl 规则并更新文档。 |
| v3 | 2026-10-25 22:54 CST | 纳入以 ada 规范化网址的讨论，压缩迁移范围、规则与验收说明。 |
| v4 | 2026-10-25 23:24 CST | 记录 Ada URL 替代的落地范围、验证结果和实际语义调整。 |
