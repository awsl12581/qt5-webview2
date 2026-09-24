# Public API quick reference / 公开接口速查

> 最后修改时间：2026-09-24 12:56 CST
> 文档版本：v5
> 修改说明：补充内置资源控制消息、快照容量核算和删除重试。

本文整理 `include/webview/` 中供应用调用的接口。最常用的入口只有一个：

```cpp
#include <webview/WebViewFactory.h>
```

该头文件同时引入 `IWebViewSession`、`IWebView` 和 `WebViewPolicy`。配置结构、事件和枚举定义在 `WebViewTypes.h` 中。

`DocumentLifetime.h`、`HostCompletion.h`、`JsonMessage.h`、`PathSecurity.h` 和 `WebViewState.h` 是库内部实现，不建议应用直接引用。

## 所有权

```cpp
using WebViewSessionPtr = std::unique_ptr<IWebViewSession>;
using WebViewPtr = std::unique_ptr<IWebView>;
using WebApplicationPtr = std::shared_ptr<const WebApplication>;
using WebViewPolicyPtr = std::shared_ptr<const WebViewPolicy>;
```

- `IWebViewSession` 管理共享的浏览器配置、缓存和 Cookie。
- `IWebView` 管理一个原生页面及其 Qt 控件。
- 销毁 session 会关闭它创建的所有 view。已经持有的 `WebViewPtr` 仍可销毁或调用 `isClosed()`，但不能继续浏览。
- 宿主负责保存 `WebViewPtr`，并在销毁对应界面前调用 `detachNativeView()` 和 `close()`。

## 创建 session

头文件：`webview/WebViewFactory.h`

```cpp
WebViewSessionPtr createWebViewSession(
    WebViewSessionOptions options,
    WebViewPolicyPtr policy = {});
```

相关配置：

```cpp
enum class SessionMode { Persistent, Ephemeral };

struct WebViewSessionOptions {
    SessionMode mode = SessionMode::Ephemeral;
    QString profilePath;
};
```

`Persistent` 保留站点数据，`Ephemeral` 使用临时会话。Windows 将 `profilePath` 作为 WebView2 用户数据目录；macOS 的公开 WKWebView API 不保证数据存放在该物理路径。

## IWebViewSession

头文件：`webview/IWebViewSession.h`

```cpp
class IWebViewSession {
public:
    using ClearCompletion = std::function<void(const WebsiteDataResult&)>;
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebViewSession() = default;
    virtual InitializationState initializationState() const = 0;
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    virtual WebApplicationPtr createApplication(WebApplicationOptions options) = 0;
    virtual WebViewPtr createWebView(QWidget* parent = nullptr) = 0;
    virtual void clearCache(ClearCompletion completion = {}) = 0;
    virtual void clearCookies(ClearCompletion completion = {}) = 0;
    virtual void clearWebsiteData(ClearCompletion completion = {}) = 0;
    virtual CapabilitySupport capabilitySupport(WebViewCapability capability) const = 0;
};
```

`createApplication()` 校验应用 ID 和页面来源。失败时返回空指针。Windows 的 WebView2 环境异步初始化，调用方可通过 `whenInitialized()` 等待 `Ready` 或接收失败原因。

清理接口的结果结构：

```cpp
struct WebsiteDataResult {
    bool success = true;
    QString error;
};
```

## 页面来源

```cpp
enum class BridgeAccess { Denied, Allowed };

struct LocalBundle {
    QString directory;
    QString entryDocument = QStringLiteral("index.html");
    bool spaFallback = true;
};

struct DevelopmentServer { QUrl url; };
struct RemoteOrigin { QUrl url; };

using ApplicationSource = std::variant<LocalBundle, DevelopmentServer, RemoteOrigin>;

struct WebApplicationOptions {
    QString id;
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

## IWebView

头文件：`webview/IWebView.h`

```cpp
class IWebView {
public:
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebView() = default;
    virtual QWidget* widget() = 0;
    virtual InitializationState initializationState() const = 0;
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    virtual void attachNativeView() = 0;
    virtual void detachNativeView() = 0;
    virtual void open(WebApplicationPtr application, const QString& route = {}) = 0;
    virtual void navigate(const QUrl& url) = 0;
    virtual void loadDocument(const QString& html, const QUrl& baseUrl = {}) = 0;
    virtual void stop() = 0;
    virtual void reload() = 0;
    virtual void close() = 0;
    virtual bool isClosed() const = 0;
    virtual WebViewBridge& bridge() = 0;
    virtual WebResourceManager& resources() = 0;
    virtual void setHostCallbacks(WebViewHostCallbacks callbacks) = 0;
};
```

推荐挂载顺序：

1. 调用 `createWebView(parent)`。
2. 把 `widget()` 放进最终的 Qt 布局。
3. 调用 `setHostCallbacks()`。
4. 调用 `open()`。
5. 激活布局后调用 `attachNativeView()`。

`close()` 可以重复调用。页面关闭后，其余浏览与消息操作不会重新激活页面。

## 初始化与加载状态

```cpp
enum class InitializationState { Initializing, Ready, Failed, Closed };

struct InitializationResult {
    InitializationState state = InitializationState::Ready;
    QString error;
};

enum class LoadState { Started, Redirected, Committed, Finished, Failed };

struct LoadEvent {
    LoadState state = LoadState::Started;
    QUrl url;
    QString error;
    quint64 navigationId = 0;
    bool isMainFrame = true;
};
```

同一次导航的事件共享一个 `navigationId`。策略在导航开始前拒绝请求时，不会伪造一组加载事件。

## 宿主回调

```cpp
struct WebViewHostCallbacks {
    std::function<void(const LoadEvent&)> load;
    std::function<void(const QUrl&)> openExternal;
    std::function<void(const NewWindowRequest&, WebViewPtr)> newWindow;
    std::function<void(const FileSelectionRequest&, FileSelectionCompletion)> selectFiles;
    std::function<void(const DownloadRequest&, DownloadCompletion)> resolveDownload;
};
```

- `load` 接收主页面加载状态。
- `openExternal` 在策略返回 `OpenExternally` 时调用。
- `newWindow` 把新建页面的所有权交给宿主。若接受该页面，必须在回调返回前保存传入的 `WebViewPtr`。
- `selectFiles` 和 `resolveDownload` 由宿主完成用户交互，并各自调用 completion 一次。

### 文件选择

```cpp
struct FileSelectionRequest {
    QUrl origin;
    QUrl documentUrl;
    bool allowsMultiple = false;
    bool allowsDirectories = false;
};

enum class FileSelectionStatus { Selected, Cancelled, Closed, InvalidResult };

struct FileSelectionResult {
    FileSelectionStatus status = FileSelectionStatus::Cancelled;
    QStringList paths;
    QString error;
};

using FileSelectionCompletion = std::function<void(FileSelectionResult)>;
```

### 下载

```cpp
struct DownloadRequest {
    QUrl url;
    QUrl origin;
    QUrl documentUrl;
    QString suggestedFileName;
};

enum class DownloadHandling { Cancel, BrowserDefault, TargetPath };

struct DownloadTarget {
    DownloadHandling handling = DownloadHandling::Cancel;
    QString filePath;
};

enum class DownloadResolutionStatus { Resolved, Cancelled, Closed, InvalidResult };

struct DownloadResolution {
    DownloadResolutionStatus status = DownloadResolutionStatus::Cancelled;
    DownloadTarget target;
    QString error;
};

using DownloadCompletion = std::function<void(DownloadResolution)>;
```

## 消息桥

页面和 C++ 通过 `WebViewBridge` 收发结构化消息：

```cpp
enum class BridgeMessageKind { Event, Request, Response };

struct BridgeMessage {
    int version = 1;
    BridgeMessageKind kind = BridgeMessageKind::Event;
    QString type;
    QString requestId;
    QJsonObject payload;
    QString error;
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
view->bridge().on(QStringLiteral("save-result"), [](const QJsonObject& payload) {
    qDebug() << payload;
});

view->bridge().emitEvent(QStringLiteral("saved"), { { "id", "42" } });

view->bridge().call(QStringLiteral("get-user"), { { "id", "42" } },
    [](const QJsonObject& payload, const QString& error) {
        if (!error.isEmpty()) qWarning() << error;
        else qDebug() << payload;
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

每种消息类型都必须在对应方向的 schema 中注册：

```cpp
webview::WebViewPolicyConfig config;
config.pageToHostSchemas.insert(
    QStringLiteral("save"),
    { { { QStringLiteral("id"), QJsonValue::String } } });
config.hostToPageSchemas.insert(
    QStringLiteral("saved"),
    { { { QStringLiteral("id"), QJsonValue::String } } });
```

`onRequest(type, handler)` 注册页面可调用的 C++ 请求处理器。处理器通过 `Reply` 返回 payload 或错误。导航或关闭会取消未完成的 C++ 请求。

## 大文件资源

大文件不放入消息 payload。`publishFile()` 在 view 已打开应用后创建稳定快照，并返回页面可读取的 opaque URL：

```cpp
const auto file = view->resources().publishFile(path, QStringLiteral("application/pdf"));
if (!file.token.isEmpty()) {
    view->bridge().emitEvent(QStringLiteral("show-file"), {
        { "url", file.url.toString() },
        { "mime", file.mimeType },
        { "token", file.token }
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

头文件：`webview/WebViewPolicy.h`

```cpp
struct BridgeMessageSchema {
    QHash<QString, QJsonValue::Type> requiredPayloadFields;
    bool allowAdditionalPayloadFields = false;
};

struct WebViewPolicyConfig {
    QSet<QString> allowedAppHosts;
    QStringList allowedFileRoots;
    QSet<QString> trustedDevelopmentOrigins;
    QSet<QString> trustedHttpsOrigins;
    QHash<QString, BridgeMessageSchema> pageToHostSchemas;
    QHash<QString, BridgeMessageSchema> hostToPageSchemas;
    int maximumBridgeMessageBytes = 64 * 1024;
};

class WebViewPolicy {
public:
    explicit WebViewPolicy(WebViewPolicyConfig config = {});
    virtual ~WebViewPolicy() = default;

    virtual NavigationDecision decideNavigation(const NavigationRequest& request) const;
    virtual NewWindowDecision decideNewWindow(const NewWindowRequest& request) const;
    virtual bool allowsBridge(const QUrl& committedUrl) const;
    virtual bool validatePageToHostMessage(
        const BridgeMessage& message, QString* error = nullptr) const;
    virtual bool validateHostToPageMessage(
        const BridgeMessage& message, QString* error = nullptr) const;
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
enum class NavigationDecision { Allow, Cancel, OpenExternally };
enum class NewWindowDecision { Allow, Cancel };
enum class PermissionDecision { Allow, Deny, Unsupported };
enum class DownloadDecision { Allow, Cancel };

enum class PermissionKind {
    Camera, Microphone, Location, Notifications, Clipboard, FilePicker
};

struct NavigationRequest {
    QUrl url;
    bool isMainFrame = true;
    bool isUserInitiated = false;
    bool isRedirect = false;
};

struct NewWindowRequest {
    QUrl url;
    bool isUserInitiated = false;
};

struct PermissionRequest {
    PermissionKind kind = PermissionKind::Camera;
    QUrl origin;
};
```

## 能力查询

```cpp
enum class CapabilitySupport { Supported, Unsupported };

enum class WebViewCapability {
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

调用 `session->capabilitySupport(capability)`，不要根据操作系统名称推断功能。具体平台差异见 [平台行为矩阵](platform-version-and-behavior-matrix.md)。

## 最小完整示例

```cpp
#include <webview/WebViewFactory.h>

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
    policyConfig.allowedAppHosts.insert(QStringLiteral("dashboard"));

    webview::WebViewSessionOptions sessionOptions;
    sessionOptions.mode = webview::SessionMode::Ephemeral;
    auto session = webview::createWebViewSession(
        std::move(sessionOptions),
        webview::createDefaultWebViewPolicy(std::move(policyConfig)));

    webview::WebApplicationOptions appOptions;
    appOptions.id = QStringLiteral("dashboard");
    appOptions.source = webview::LocalBundle { QStringLiteral("/path/to/dist") };
    auto application = session->createApplication(std::move(appOptions));
    if (!application) {
        return 1;
    }

    auto view = session->createWebView(&window);
    layout->addWidget(view->widget());

    webview::WebViewHostCallbacks callbacks;
    callbacks.load = [](const webview::LoadEvent& event) {
        if (event.state == webview::LoadState::Failed) {
            qWarning() << event.error;
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

