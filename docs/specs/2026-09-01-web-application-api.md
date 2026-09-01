---
mission: spec
status: approved
created: 2026-09-01
approved_at: 2026-09-01T20:48:24+08:00
---

# Web application API

## Goal

把系统 WebView 的公共加载模型从 URL 与本地资源映射改为应用。使用 Vite 的开发者应能以同一种调用方式打开开发服务器、打包后的 `dist` 目录和线上应用，而不需要了解 `app://`、资源映射或原生 scheme handler。

## Scope

- 在公共头中引入 `WebApplication`、`ApplicationSource`、`LocalBundle`、`DevelopmentServer` 和 `RemoteOrigin`。
- `IWebViewSession` 创建并拥有应用；`IWebView` 通过 `open(application, route)` 打开应用和客户端路由。
- `WebViewSessionOptions` 只保留 profile 与 session 行为，不再承载应用资源配置。
- 本地 bundle 使用稳定的私有 `app://<application-id>/` origin。资源目录映射、路径规范化、MIME 推断、路径穿越与 symlink escape 防护留在内部实现。
- 本地 bundle 支持入口文档和 SPA fallback。仅主文档导航可回退到入口文档；缺失的脚本、样式、图片和 fetch 请求必须继续失败。
- 更新 demo、文档和测试，使 Vite `dist` 为本地应用的标准例子。

## Design

公共 API 的对象关系如下：

```cpp
webview::WebApplicationOptions app;
app.id = QStringLiteral("demo");
app.source = webview::LocalBundle {
    QStringLiteral("/absolute/path/to/dist"),
    QStringLiteral("index.html"),
    true,
};
app.bridgeAccess = webview::BridgeAccess::Allowed;

auto application = session->createApplication(std::move(app));
auto view = session->createWebView(parent);
view->attachNativeView();
view->open(application, QStringLiteral("/orders/42"));
```

`WebApplication` 是 session 作用域的值或只读句柄。它包含稳定应用 ID、来源和 bridge 授权。应用 ID 决定本地 bundle 的内部 origin，不能由路由或请求 URL 改变。`open` 只接受应用句柄和应用内 route，负责定位入口文档或来源根 URL；它不接受任意文件系统路径。

`LocalBundle` 指向 Vite 构建结果目录。它把入口文档和引用的静态资源提供给内部 `app://` origin。`spaFallback` 打开时，未知的 HTML 文档路由返回入口文档，静态资源与 API 请求不回退。

`DevelopmentServer` 与 `RemoteOrigin` 都以明确的根 URL 表示内容来源。它们不启动、代理或猜测本地 server。前者用于例如 `http://127.0.0.1:5173` 的 Vite server，后者用于 HTTPS 部署。两者调用 `open` 的方式与 `LocalBundle` 相同。

bridge 授权属于应用声明，不由 URL 字符串或开发模式隐式推断。应用来源和该授权仍需接受 session 的 `WebViewPolicy`；未授权、重定向后的不匹配 origin、子 frame 与过期文档均不得取得 native bridge 权限。

通用网页浏览仍使用 `navigate(QUrl)`。`setHtml` 不再承担应用部署职责；若保留动态文档能力，公共名称改为 `loadDocument`，并要求显式、受 policy 约束的文档 origin。

## Compatibility

这是一次 source-breaking 替换。删除公共 `WebResourceMapping`、`WebViewSessionOptions::resourceMappings` 与应用调用方对它们的依赖。不得保留旧 API 的 adapter、deprecated overload、feature flag、双注册或 fallback。

旧 `WebResourceMapping` 的完整职责由 `LocalBundle` 的内部资源提供路径接管。`app://` URL、资源解析器和各平台的 scheme/resource-request handler 不再出现在应用代码、demo 或公共头中。

## Security

本地 bundle 在创建应用时验证应用 ID、入口相对路径和 canonical root。请求解析继续拒绝 percent-encoded traversal、root 外路径、symlink escape、目录与不存在文件。应用间不得共享 origin。

Development server 与 remote application 的 origin 必须是明确 URL，不能接受宽泛 host pattern 或运行时字符串拼接出的隐式信任。`open` 生成的 URL 经过已有导航 policy；任何跨 origin 导航仍由 policy 单独决定。

## Acceptance Criteria

- 公共头和应用代码中不存在 `WebResourceMapping`、`resourceMappings` 或手写 `app://` 加载 URL。
- 应用可以通过同一组 `createApplication`、`createWebView`、`open` 调用打开本地 Vite `dist`、Vite development server 和 HTTPS remote origin。
- `LocalBundle` 的 HTML、CSS、JavaScript、图片及 fetch 可从 bundle 正确加载；无效路径、目录、缺失文件、路径穿越和 symlink escape 被拒绝。
- SPA route 会加载入口文档；缺失静态资源与 fetch 请求不会回退到入口文档。
- bridge 仅在已授权应用的当前顶层文档中可用，且沿用现有 token、origin 与消息 schema 检查。
- macOS 和 Windows backend 通过相同的公共应用 API 实现本地 bundle；不支持所需能力时报告明确初始化或加载失败，不启动隐式 HTTP server。
- demo、架构文档、安装示例和相关测试全部使用新模型；旧公共类型、配置、调用链和兼容代码已删除。
