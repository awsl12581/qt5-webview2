# Public API quick reference / 公开接口速查

> 最后修改时间：2026-10-26 14:49 CST
> 文档版本：v18
> 修改说明：说明 WebViewHost 动态刷新宿主句柄、尺寸和可见性。

公开接口全部声明在 `include/system_webview/system_webview.h`，使用方只需引入：

```cpp
#include <system_webview/system_webview.h>
```

该头文件包含 `IWebViewSession`、`IWebView`、`WebViewPolicy`、配置结构、事件和枚举。项目构建动态库；
`SYSTEM_WEBVIEW_API` 在 Windows 上切换 `dllexport` 和 `dllimport`，在支持符号可见性的其他工具链上导出标注的 API。
`WebViewBridge`、`WebResourceManager` 和 `WebViewPolicy` 使用 PImpl 保存库内状态，公开对象布局不包含这些实现字段。
所有公开枚举均为 `enum class ... : int`，头文件在每个枚举项旁说明其语义。

`src/` 下的头文件属于库内部实现，不安装给使用方。

接口中的文本和文件路径统一使用 UTF-8 `std::string`；URL 使用 `webview::Url`，由 Ada 按 WHATWG 规则解析和序列化；JSON 使用 `webview::Json`（`nlohmann::json` 别名）。宿主窗口通过框架无关的 `WebViewHost` 描述传入，Qt、GLFW、SDL 和平台原生适配器各自提供实现。JSON 的键和字符串直接使用 UTF-8；Qt 界面文本在显示时显式转换。

Windows 原生边界使用 simdutf 严格转换 UTF-8/UTF-16，非法编码不替换为占位字符。传给 WebView2 和 Win32 的 NUL 结尾文本拒绝嵌入 NUL；编码失败按对应接口报告发送失败、初始化/导航失败、资源错误或取消下载。macOS 保留 Foundation 转换。

这次迁移改变了源码和二进制接口。调用方必须更新参数、回调和策略覆盖方法的类型，并重新构建；不提供旧 Qt 值类型重载。`BridgeTransport::send()` 和 `WebViewBridge::receive()` 现在使用 `const std::string&` 传递完整的 UTF-8 JSON 字节序列。

## 所有权

```cpp
using WebViewSessionPtr = std::unique_ptr<IWebViewSession>;
using WebViewPtr = std::unique_ptr<IWebView>;
using WebApplicationPtr = std::shared_ptr<const WebApplication>;
using WebViewPolicyPtr = std::shared_ptr<const WebViewPolicy>;
```

- `IWebViewSession` 管理共享的浏览器配置、缓存和 Cookie。
- `IWebView` 管理一个原生页面及其外部宿主。
- 销毁 session 会关闭它创建的所有 view。已经持有的 `WebViewPtr` 仍可销毁或调用 `isClosed()`，但不能继续浏览。
- 宿主负责保存 `WebViewPtr`，并在销毁对应界面前调用 `detachNativeView()` 和 `close()`。

## 创建 session

头文件：`system_webview/system_webview.h`

```cpp
WebViewSessionPtr createWebViewSession(
    WebViewSessionOptions options,
    WebViewPolicyPtr policy = {});
```

相关配置：

```cpp
enum class SessionMode : int { Persistent, Ephemeral };

struct WebViewSessionOptions {
    SessionMode mode = SessionMode::Ephemeral;
    std::string profilePath;
};
```

`Persistent` 保留站点数据，`Ephemeral` 使用临时会话。Windows 将 `profilePath` 作为 WebView2 用户数据目录；macOS 的公开 WKWebView API 不保证数据存放在该物理路径。

接口中的文本和文件路径统一使用 UTF-8 `std::string`；URL 使用 `webview::Url`，由 Ada 按 WHATWG 规则解析和序列化；JSON 使用 `webview::Json`（`nlohmann::json` 别名）。宿主窗口通过框架无关的 `WebViewHost` 描述传入，Qt、GLFW、SDL 和平台原生适配器各自提供实现。JSON 的键和字符串直接使用 UTF-8；Qt 界面文本在显示时显式转换。

这次迁移改变了源码和二进制接口。调用方必须更新参数、回调和策略覆盖方法的类型，并重新构建；不提供旧 Qt 值类型重载。`BridgeTransport::send()` 和 `WebViewBridge::receive()` 现在使用 `const std::string&` 传递完整的 UTF-8 JSON 字节序列。

## IWebViewSession

头文件：`system_webview/system_webview.h`

```cpp
class IWebViewSession {
public:
    using ClearCompletion = std::function<void(const WebsiteDataResult&)>;
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebViewSession() = default;
    virtual InitializationState initializationState() const = 0;
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    virtual WebApplicationPtr createApplication(WebApplicationOptions options) = 0;
    virtual WebViewPtr createWebView(WebViewHost host) = 0;
    virtual void clearCache(ClearCompletion completion = {}) = 0;
    virtual void clearCookies(ClearCompletion completion = {}) = 0;
    virtual void clearWebsiteData(ClearCompletion completion = {}) = 0;
    virtual void setHostCallbacks(WebViewSessionHostCallbacks callbacks) = 0;
    virtual bool supports(WebViewCapability capability) const = 0;
};
```

`createApplication()` 校验应用 ID 和页面来源。失败时返回空指针。Windows 的 WebView2 环境异步初始化，调用方可通过 `whenInitialized()` 等待 `Ready` 或接收失败原因。

应用 ID 必须为非空、合法的 UTF-8，只接受 Unicode 字母、数字（类别 `L*`、`N*`）和 ASCII 连字符 `-`，由 utf8proc 2.12.0 按 Unicode 18.0.0 码点分类，包括补充平面字母和数字。空白、下划线、组合标记、emoji 和嵌入 NUL 均拒绝。不自动规范化或折叠大小写，也不限制连字符位置；例如 `é` 合法，分解形式 `e` 加组合重音不合法。相较于旧 Qt 实现，补充平面字母/数字和新 Unicode 数据中的字母/数字可以获得支持。策略配置中的 `allowedAppHosts` 使用 Ada 序列化后的 host，Unicode ID 的 URL host 可能为百分号编码形式。

清理接口的结果结构：

```cpp
struct WebsiteDataResult {
    bool success = true;
    std::string error;
};
```

## 页面来源

```cpp
enum class BridgeAccess : int { Denied, Allowed };
enum class ExternalNetworkAccess : int { Denied, Allowed };

struct LocalBundle {
    std::string directory;
    std::string entryDocument = "index.html";
    bool spaFallback = true;
    ExternalNetworkAccess externalNetworkAccess = ExternalNetworkAccess::Denied;
};

struct DevelopmentServer { webview::Url url; };
struct RemoteOrigin { webview::Url url; };

using ApplicationSource = std::variant<LocalBundle, DevelopmentServer, RemoteOrigin>;

struct WebApplicationOptions {
    std::string id;
    ApplicationSource source;
    BridgeAccess bridgeAccess = BridgeAccess::Denied;
};
```

| 来源 | 用途 | 策略要求 |
| --- | --- | --- |
| `LocalBundle` | 打包后的 HTML、CSS 和 JavaScript | `id` 必须加入 `allowedAppHosts` |
| `DevelopmentServer` | 本地开发服务器 | HTTP 来源必须加入 `trustedDevelopmentOrigins` |
| `RemoteOrigin` | 已部署的 HTTPS 页面 | 使用桥接时必须加入 `trustedHttpsOrigins` |

只有 `bridgeAccess == Allowed` 且当前来源受策略信任时，页面桥接才可用。

`LocalBundle` 默认使用 `ExternalNetworkAccess::Denied`。后端会在本地 HTML 响应中加入内容安全策略，阻止页面向外部来源发起 `fetch`、XHR、WebSocket、子资源加载和表单提交。同一 `app://` 来源可以正常使用；策略还为需要内嵌数据的资源类型保留了 `data:` 或 `blob:`。确实需要访问外部网络时，应显式设置 `ExternalNetworkAccess::Allowed`。

这个选项不决定顶层导航。链接跳转、重定向和新窗口仍由 `WebViewPolicy` 处理。`DevelopmentServer` 和 `RemoteOrigin` 的响应头由对应服务器管理，不受此选项影响。

## IWebView

头文件：`system_webview/system_webview.h`

```cpp
class IWebView {
public:
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebView() = default;
    virtual InitializationState initializationState() const = 0;
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    virtual void attachNativeView() = 0;
    virtual void detachNativeView() = 0;
    virtual void open(WebApplicationPtr application, const std::string& route = {}) = 0;
    virtual void navigate(const webview::Url& url) = 0;
    virtual void loadDocument(const std::string& html, const webview::Url& baseUrl = {}) = 0;
    virtual void stop() = 0;
    virtual void reload() = 0;
    virtual void close() = 0;
    virtual bool isClosed() const = 0;
    virtual WebViewBridge& bridge() = 0;
    virtual WebResourceManager& resources() = 0;
    virtual void setHostCallbacks(WebViewHostCallbacks callbacks) = 0;
};
```

推荐挂载顺序（Qt）：

1. 创建 `QtHostWidget` 并将其放进最终的 Qt 布局。
2. 调用 `createWebView(host.webViewHost())`。
3. 调用 `setHostCallbacks()` 和 `open()`。
4. 激活布局后调用 `attachNativeView()`。

`close()` 可以重复调用。页面关闭后，其余浏览与消息操作不会重新激活页面。

## 初始化与加载状态

```cpp
enum class InitializationState : int { Initializing, Ready, Failed, Closed };

struct InitializationResult {
    InitializationState state = InitializationState::Ready;
    std::string error;
};

enum class LoadState : int { Started, Redirected, Committed, Finished, Failed };

struct LoadEvent {
    LoadState state = LoadState::Started;
    webview::Url url;
    std::string error;
    std::uint64_t navigationId = 0;
    bool isMainFrame = true;
};
```

同一次导航的事件共享一个 `navigationId`。策略在导航开始前拒绝请求时，不会伪造一组加载事件。

## 宿主回调

```cpp
struct WebViewHostCallbacks {
    std::function<void(const LoadEvent&)> onLoad;
    std::function<void(const webview::Url&)> onOpenExternal;
    std::function<WebViewHost(const NewWindowRequest&)> createPopupHost;
    std::function<void(const NewWindowRequest&, WebViewPtr)> onNewWindow;
    std::function<void(const FileSelectionRequest&, FileSelectionCompletion)> onSelectFiles;
    std::function<void(const DownloadRequest&, DownloadCompletion)> onResolveDownload;
    std::function<void(const RuntimeFailureEvent&)> onRuntimeFailure;
};

struct WebViewSessionHostCallbacks {
    std::function<void(const RuntimeFailureEvent&)> onRuntimeFailure;
};
```

- `onLoad` 接收主页面加载状态。
- `onOpenExternal` 在策略返回 `OpenExternally` 时调用。
- `createPopupHost` 在允许创建 popup 前提供子页面宿主。若返回的 `WebViewHost` 没有有效原生句柄，popup 会被取消。
- `onNewWindow` 把新建页面的所有权交给宿主。若接受该页面，必须在回调返回前保存传入的 `WebViewPtr`。
- `onSelectFiles` 和 `onResolveDownload` 由宿主完成用户交互，并各自调用 completion 一次。
- view 的 `onRuntimeFailure` 接收 WebView2 `ProcessFailed` 或 WKWebView 内容进程终止事件；session 的同名回调接收 WebView2 环境报告的 browser process 故障。同一次 WebView2 browser process 故障可能在两个作用域各报告一次。

运行时故障使用固定枚举，不要求调用方解析文本：

```cpp
enum class RuntimeFailureKind : int {
    WebContentProcessTerminated,
    WebContentProcessUnresponsive,
    BrowserProcessTerminated,
    AuxiliaryProcessTerminated,
    Unknown
};

struct RuntimeFailureEvent {
    RuntimeFailureKind kind = RuntimeFailureKind::Unknown;
    std::string reason;
    std::int64_t nativeCode = 0;
};
```

关闭后的 session 或 view 不再触发运行时故障回调。

### 文件选择

```cpp
struct FileSelectionRequest {
    webview::Url origin;
    webview::Url documentUrl;
    bool allowsMultiple = false;
    bool allowsDirectories = false;
};

enum class FileSelectionStatus : int { Selected, Cancelled, Closed, InvalidResult };

struct FileSelectionResult {
    FileSelectionStatus status = FileSelectionStatus::Cancelled;
    std::vector<std::string> paths;
    std::string error;
};

using FileSelectionCompletion = std::function<void(FileSelectionResult)>;
```

### 下载

```cpp
struct DownloadRequest {
    webview::Url url;
    webview::Url origin;
    webview::Url documentUrl;
    std::string suggestedFileName;
};

enum class DownloadHandling : int { Cancel, BrowserDefault, TargetPath };

struct DownloadTarget {
    DownloadHandling handling = DownloadHandling::Cancel;
    std::string filePath;
};

enum class DownloadResolutionStatus : int { Resolved, Cancelled, Closed, InvalidResult };

struct DownloadResolution {
    DownloadResolutionStatus status = DownloadResolutionStatus::Cancelled;
    DownloadTarget target;
    std::string error;
};

using DownloadCompletion = std::function<void(DownloadResolution)>;
```

## 诊断日志

库通过 `DiagnosticEvent` 的 `category` 字段输出 `system_webview.lifecycle`、`navigation`、`policy`、`bridge`、`resource` 和 `runtime` 六类诊断。调用方在 `WebViewSessionOptions::diagnosticHandler` 中接收事件并自行过滤、格式化和持久化；库不依赖 Qt 日志设施，也不创建日志文件。

库不创建日志或 dump 文件，也不记录 URL path/query、Bridge payload、token 或本地绝对路径。完整事件码和 dump 接入边界见 `docs/guides/diagnostics.md`。

## 消息桥

页面和 C++ 通过 `WebViewBridge` 收发结构化消息：

```cpp
enum class BridgeMessageKind : int { Event, Request, Response };

struct BridgeMessage {
    int version = 1;
    BridgeMessageKind kind = BridgeMessageKind::Event;
    std::string type;
    std::string requestId;
    webview::Json payload = webview::Json::object();
    std::string error;
};
```

页面发送到 C++：

```js
window.systemWebView.postMessage({
  version: 1,
  kind: "event",
  type: "save",
  payload: { id: "42" }
});
```

C++ 注册事件、发事件或发起请求：

```cpp
view->bridge().setEventHandler("save-result", [](const webview::Json& payload) {
    qDebug() << QString::fromStdString(payload.dump());
});

view->bridge().emitEvent("saved", { { "id", "42" } });

view->bridge().call("get-user", { { "id", "42" } },
    [](const webview::Json& payload, const std::string& error) {
        if (!error.empty()) qWarning() << QString::fromStdString(error);
        else qDebug() << QString::fromStdString(payload.dump());
    });
```

页面接收 C++ 消息：

```js
const stopListening = window.systemWebView.on("saved", payload => {
  console.log(payload.id);
});
// stopListening() removes this listener.
```

`on()` 对事件和请求都生效。C++ 发起请求时，处理器可通过第二个参数的 `reply(payload, error)` 应答。兼容的 `system-webview-message` DOM 事件仍会派发。

JSON 类型与规则：`JsonType` 包含 `Null`、`Boolean`、`Number`、`String`、`Array`、`Object`；`Number` 接受有符号整数、无符号整数及浮点数。缺失字段与 null 分开判断。payload 必须是对象，空对象使用 `webview::Json::object()`，不能用默认构造的 null 代替。

Bridge 使用严格 UTF-8，拒绝非法 JSON、非有限数字和超过 128 层的嵌套。协议版本必须数值恰为 1（`1`、`1.0` 均可），重复键采用后值覆盖。默认 64 KiB 限制继续检查进入 Bridge 的字节数和统一编码后的 envelope；序列化格式以新库为准。请求编码失败会完成错误回调；回复无法编码时尝试发送空对象 payload 的错误响应。

nlohmann/json 3.12.0 是公开头文件依赖，由项目 CMake 目标传播并随安装包提供。使用方需匹配 JSON 版本、类型配置宏、编译器、标准库和 Windows CRT，并重编译；本接口不承诺稳定 DLL ABI。

每种消息类型都必须在对应方向的 schema 中注册：

```cpp
webview::WebViewPolicyConfig config;
config.pageToHostSchemas["save"] = { { { "id", webview::JsonType::String } } };
config.hostToPageSchemas["saved"] = { { { "id", webview::JsonType::String } } };
```

`setEventHandler(type, handler)` 和 `setRequestHandler(type, handler)` 分别为每个类型设置唯一处理器；传入空处理器会移除已有处理器。请求处理器通过 `Reply` 返回 payload 或错误。导航或关闭会取消未完成的 C++ 请求。

## 大文件资源

大文件不放入消息 payload。`publishFile()` 在 view 已打开应用后创建稳定快照，并返回页面可读取的 opaque URL：

```cpp
const auto file = view->resources().publishFile(path, "application/pdf");
if (!file.token.empty()) {
    view->bridge().emitEvent("show-file", {
        { "url", file.url.toString() },
        { "mime", QString::fromStdString(file.mimeType) },
        { "token", QString::fromStdString(file.token) }
    });
}
```

页面将 URL 用作 `img`、`video` 或 `iframe` 的资源地址。不再展示时先清除 DOM 引用，再通知原生端释放：

```js
frame.removeAttribute("src");
window.systemWebView.postMessage({ type: "release-resource", payload: { token } });
```

`release-resource` 是内置的页面到宿主事件；宿主也可调用 `view->resources().release(token)`，并通过内置 `resource-revoked` 事件通知页面。这两种消息不需要在应用 schema 中重复注册。资源按当前 document 隔离，支持单段 Range；导航、view 关闭和 session 销毁会撤销该 document 的资源。每个 token 发布 30 分钟后过期，过期项在下一次发布时清理。发布的源文件由调用方拥有，manager 只删除自己创建的快照。活动读取持有快照租约，快照删除成功前仍计入总容量；删除失败会定时重试，也会在下一次发布时重试。

默认不允许额外字段。确实需要扩展 payload 时，将 `BridgeMessageSchema::allowAdditionalPayloadFields` 设为 `true`。

## WebViewPolicy

头文件：`system_webview/system_webview.h`

```cpp
struct BridgeMessageSchema {
    std::unordered_map<std::string, webview::JsonType> requiredPayloadFields;
    bool allowAdditionalPayloadFields = false;
};

struct WebViewPolicyConfig {
    std::unordered_set<std::string> allowedAppHosts;
    std::vector<std::string> allowedFileRoots;
    std::unordered_set<std::string> trustedDevelopmentOrigins;
    std::unordered_set<std::string> trustedHttpsOrigins;
    std::unordered_map<std::string, BridgeMessageSchema> pageToHostSchemas;
    std::unordered_map<std::string, BridgeMessageSchema> hostToPageSchemas;
    int maximumBridgeMessageBytes = 64 * 1024;
};

class WebViewPolicy {
public:
    explicit WebViewPolicy(WebViewPolicyConfig config = {});
    WebViewPolicy(const WebViewPolicy& other);
    WebViewPolicy& operator=(const WebViewPolicy& other);
    virtual ~WebViewPolicy();

    virtual NavigationDecision decideNavigation(const NavigationRequest& request) const;
    virtual NewWindowDecision decideNewWindow(const NewWindowRequest& request) const;
    virtual bool allowsBridge(const webview::Url& committedUrl) const;
    virtual bool validatePageToHostMessage(
        const BridgeMessage& message, std::string* error = nullptr) const;
    virtual bool validateHostToPageMessage(
        const BridgeMessage& message, std::string* error = nullptr) const;
    virtual PermissionDecision decidePermission(const PermissionRequest& request) const;
    virtual DownloadDecision decideDownload(const DownloadRequest& request) const;

protected:
    const WebViewPolicyConfig& config() const;
};

WebViewPolicyPtr createDefaultWebViewPolicy(WebViewPolicyConfig config = {});
```

默认策略允许普通 HTTPS 导航，拒绝未列入配置的 HTTP、`app://`、`file://`、弹窗、下载和权限请求。需要自定义决策时，继承 `WebViewPolicy` 并覆盖对应方法。

相关决策类型：

```cpp
enum class NavigationDecision : int { Allow, Cancel, OpenExternally };
enum class NewWindowDecision : int { Allow, Cancel };
enum class PermissionDecision : int { Allow, Deny, Unsupported };
enum class DownloadDecision : int { Allow, Cancel };

enum class PermissionKind : int {
    Camera, Microphone, Location, Notifications, Clipboard, FilePicker
};

struct NavigationRequest {
    webview::Url url;
    bool isMainFrame = true;
    bool isUserInitiated = false;
    bool isRedirect = false;
};

struct NewWindowRequest {
    webview::Url url;
    bool isUserInitiated = false;
};

struct PermissionRequest {
    PermissionKind kind = PermissionKind::Camera;
    webview::Url origin;
};
```

## 能力查询

```cpp
enum class WebViewCapability : int {
    PersistentProfile,
    PrivateProfile,
    FileSelection,
    DownloadDefault,
    DownloadTarget,
    Camera,
    Microphone,
    Location,
    Notifications,
    Clipboard
};
```

调用 `session->supports(capability)`，不要根据操作系统名称推断功能。具体平台差异见 [平台行为矩阵](platform-version-and-behavior-matrix.md)。

## 最小完整示例

```cpp
#include <system_webview/system_webview.h>
#include <system_webview/qt_host.h>

#include <QApplication>
#include <QDebug>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

int main(int argc, char* argv[])
{
    QApplication qtApp(argc, argv);
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);

    webview::WebViewPolicyConfig policyConfig;
    policyConfig.allowedAppHosts.insert("dashboard");

    webview::WebViewSessionOptions sessionOptions;
    sessionOptions.mode = webview::SessionMode::Ephemeral;
    auto session = webview::createWebViewSession(
        std::move(sessionOptions),
        webview::createDefaultWebViewPolicy(std::move(policyConfig)));

    webview::WebApplicationOptions appOptions;
    appOptions.id = "dashboard";
    appOptions.source = webview::LocalBundle { "/path/to/dist" };
    auto application = session->createApplication(std::move(appOptions));
    if (!application) {
        return 1;
    }

    auto* host = new webview::QtHostWidget(&window);
    layout->addWidget(host);
    auto view = session->createWebView(host->webViewHost());

    webview::WebViewHostCallbacks callbacks;
    callbacks.onLoad = [](const webview::LoadEvent& event) {
        if (event.state == webview::LoadState::Failed) {
            qWarning() << QString::fromStdString(event.error);
        }
    };
    view->setHostCallbacks(std::move(callbacks));
    view->open(application);

    window.resize(1000, 700);
    window.show();
    layout->activate();
    view->attachNativeView();
    return qtApp.exec();
}
```

## 版本修改记录

| 版本 | 修改时间 | 修改内容 |
| --- | --- | --- |
| v1 | 创建时 | 初始公开接口速查。 |
| v2 | 2026-09-24 04:37 CEST | 补充统一文档元数据和版本记录。 |
| v3 | 2026-09-24 12:10 CST | 替换旧消息回调文档，增加 Bridge 请求/响应和大文件资源 API。 |
| v4 | 2026-09-24 12:26 CST | 补齐页面端 `on()` API、消息大小校验和资源过期说明。 |
| v5 | 2026-09-24 12:56 CST | 补充资源释放消息、活动读取的容量核算及删除失败重试。 |
| v6 | 2026-09-24 17:29 CST | 统一公开头文件路径，并说明动态库导出和 PImpl 边界。 |
| v7 | 2026-09-24 17:57 CST | 同步头文件中的使用说明，并简化能力查询和回调注册命名。 |
| v8 | 2026-09-24 18:05 CST | 说明公开枚举的固定底层类型，并同步枚举声明。 |
| v9 | 2026-09-24 18:17 CST | 增加本地页面的外部网络访问控制，并说明默认拒绝行为。 |
| v10 | 2026-09-24 18:29 CST | 增加运行时故障公共类型、session/view 回调和诊断日志说明。 |
| v11 | 2026-09-24 18:53 CST | 明确 WebView2 browser process 故障在 view 和 session 两个作用域的回调语义。 |
| v12 | 2026-10-25 22:20 CST | 将公开字符串、容器和整数类型同步为标准库类型，并说明 UTF-8 和接口迁移要求。 |
| v13 | 2026-10-25 23:24 CST | 将公开 URL 参数和字段同步为 Ada-backed `webview::Url`。 |
| v14 | 2026-10-26 10:58 CST | 说明 utf8proc 应用 ID 校验及 Unicode 码点规则。 |
| v15 | 2026-10-26 11:10 CST | 补充 Windows 原生文本边界的严格编码和 NUL 规则。 |
| v16 | 2026-10-26 11:29 CST | 同步 nlohmann/json 公开类型、schema 和严格 JSON 规则。 |
| v17 | 2026-10-26 14:20 CST | 同步 WebViewHost popup 宿主交接接口，移除 Qt 宿主依赖说明。 |
| v18 | 2026-10-26 14:49 CST | 说明 WebViewHost 动态刷新宿主句柄、尺寸和可见性。 |
