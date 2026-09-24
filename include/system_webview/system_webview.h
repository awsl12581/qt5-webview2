#pragma once

/**
 * \file system_webview.h
 * Single public header for embedding the system web view in a Qt Widgets app.
 *
 * Typical use:
 * \code
 * webview::WebViewSessionOptions sessionOptions;
 * auto session = webview::createWebViewSession(std::move(sessionOptions));
 *
 * webview::WebApplicationOptions appOptions;
 * appOptions.id = QStringLiteral("main");
 * appOptions.source = webview::LocalBundle { resourceDirectory };
 * auto application = session->createApplication(std::move(appOptions));
 * if (!application)
 *     return;
 *
 * auto view = session->createWebView(parentWidget);
 * layout->addWidget(view->widget());
 * view->open(application);
 * layout->activate();
 * view->attachNativeView();
 * \endcode
 *
 * Keep the session alive while any application or view created by it is in use.
 * Before destroying the host widget, call detachNativeView() and close() on the
 * view. See the individual types below for bridge, policy, and callback usage.
 */

#if defined(_WIN32)
#if defined(SYSTEM_WEBVIEW_BUILDING_LIBRARY)
#define SYSTEM_WEBVIEW_API __declspec(dllexport)
#else
#define SYSTEM_WEBVIEW_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SYSTEM_WEBVIEW_API __attribute__((visibility("default")))
#else
#define SYSTEM_WEBVIEW_API
#endif

#include <QByteArray>
#include <QHash>
#include <QIODevice>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>
#include <memory>
#include <utility>
#include <variant>

class QWidget;

namespace webview
{
class IWebView;
/// Owns one web view. Destroying it closes the underlying native view.
using WebViewPtr = std::unique_ptr<IWebView>;
class WebApplication;
/// Immutable application source registered with a session.
using WebApplicationPtr = std::shared_ptr<const WebApplication>;

enum class LoadState : int
{
    Started,    ///< The main-frame navigation has started.
    Redirected, ///< The navigation received a server-side redirect.
    Committed,  ///< The destination document has begun loading into the view.
    Finished,   ///< The destination document finished loading successfully.
    Failed      ///< The navigation stopped with LoadEvent::error set.
};

enum class InitializationState : int
{
    Initializing, ///< Native browser state is still being created.
    Ready,        ///< The session or view can accept operations.
    Failed,       ///< Initialization ended permanently; see InitializationResult::error.
    Closed        ///< The session or view has been shut down.
};

struct InitializationResult
{
    InitializationState state = InitializationState::Ready;
    QString error;
};

struct LoadEvent
{
    LoadState state = LoadState::Started;
    QUrl url;
    QString error;
    quint64 navigationId = 0;
    bool isMainFrame = true;
};

enum class NavigationDecision : int
{
    Allow,         ///< Continue navigation in the current view.
    Cancel,        ///< Cancel navigation without handing the URL to the host.
    OpenExternally ///< Cancel in-view navigation and invoke onOpenExternal.
};

struct NavigationRequest
{
    QUrl url;
    bool isMainFrame = true;
    bool isUserInitiated = false;
    bool isRedirect = false;
};

struct NewWindowRequest
{
    QUrl url;
    bool isUserInitiated = false;
};

enum class NewWindowDecision : int
{
    Allow, ///< Create a child view and transfer it through onNewWindow.
    Cancel ///< Suppress the requested popup.
};

enum class PermissionDecision : int
{
    Allow,      ///< Grant the requested browser permission.
    Deny,       ///< Deny a permission supported by the backend.
    Unsupported ///< The portable backend cannot represent this permission.
};

enum class PermissionKind : int
{
    Camera,        ///< Video-capture device access.
    Microphone,    ///< Audio-capture device access.
    Location,      ///< Geolocation access.
    Notifications, ///< System notification access.
    Clipboard,     ///< Page clipboard access beyond ordinary editing operations.
    FilePicker     ///< Host-mediated local file or directory selection.
};

enum class DownloadDecision : int
{
    Allow, ///< Ask onResolveDownload how the download should be handled.
    Cancel ///< Cancel the download before consulting the host.
};

/// Optional platform features queried with IWebViewSession::supports().
enum class WebViewCapability : int
{
    PersistentProfile, ///< Website data can persist across session instances.
    PrivateProfile,    ///< Website data can be isolated in a non-persistent profile.
    FileSelection,     ///< File-picker requests can be delegated to the host.
    DownloadDefault,   ///< Downloads can use the browser's default destination.
    DownloadTarget,    ///< The host can provide an explicit download path.
    Camera,            ///< Camera permission requests can be represented.
    Microphone,        ///< Microphone permission requests can be represented.
    Location,          ///< Geolocation permission requests can be represented.
    Notifications,     ///< Notification permission requests can be represented.
    Clipboard          ///< Clipboard permission requests can be represented.
};

enum class SessionMode : int
{
    Persistent, ///< Reuse persistent website data managed by the backend.
    Ephemeral   ///< Keep website data in a non-persistent private session.
};

enum class BridgeAccess : int
{
    Denied, ///< Do not expose the native bridge to this application.
    Allowed ///< Expose the bridge only while policy also trusts the origin.
};

enum class ExternalNetworkAccess : int
{
    Denied, ///< Add a host policy that restricts external connections and resources.
    Allowed ///< Do not add the host-provided external network restriction.
};

struct LocalBundle
{
    /// Absolute directory containing the bundle.
    QString directory;
    /// Document served when a route resolves to a directory.
    QString entryDocument = QStringLiteral("index.html");
    /// Fall back to entryDocument for routes without a matching file.
    bool spaFallback = true;
    /// Controls external subresources, fetch, XHR, WebSocket, and form targets.
    ExternalNetworkAccess externalNetworkAccess = ExternalNetworkAccess::Denied;
};

struct DevelopmentServer
{
    QUrl url;
};

struct RemoteOrigin
{
    QUrl url;
};

using ApplicationSource = std::variant<LocalBundle, DevelopmentServer, RemoteOrigin>;

struct WebApplicationOptions
{
    /// Unique within the session; also becomes the host of a local app:// URL.
    QString id;
    ApplicationSource source;
    /// Bridge access still requires the committed origin to be trusted by policy.
    BridgeAccess bridgeAccess = BridgeAccess::Denied;
};

struct WebViewSessionOptions
{
    SessionMode mode = SessionMode::Ephemeral;
    /// User-data directory for a persistent WebView2 profile; ignored by WKWebView.
    QString profilePath;
};

struct PermissionRequest
{
    PermissionKind kind = PermissionKind::Camera;
    QUrl origin;
};

struct DownloadRequest
{
    QUrl url;
    QUrl origin;
    QUrl documentUrl;
    QString suggestedFileName;
};

struct FileSelectionRequest
{
    QUrl origin;
    QUrl documentUrl;
    bool allowsMultiple = false;
    bool allowsDirectories = false;
};

enum class FileSelectionStatus : int
{
    Selected,     ///< paths contains the host-approved selection.
    Cancelled,    ///< The user or host cancelled selection.
    Closed,       ///< The view closed before selection completed.
    InvalidResult ///< The host returned paths that failed validation.
};

struct FileSelectionResult
{
    FileSelectionStatus status = FileSelectionStatus::Cancelled;
    QStringList paths;
    QString error;
};

using FileSelectionCompletion = std::function<void(FileSelectionResult)>;

enum class DownloadHandling : int
{
    Cancel,         ///< Cancel the download.
    BrowserDefault, ///< Let the browser choose its default destination.
    TargetPath      ///< Save to DownloadTarget::filePath.
};

struct DownloadTarget
{
    DownloadHandling handling = DownloadHandling::Cancel;
    QString filePath;
};

enum class DownloadResolutionStatus : int
{
    Resolved,     ///< target contains the host's chosen handling.
    Cancelled,    ///< The user or host cancelled the download.
    Closed,       ///< The view closed before the host resolved the download.
    InvalidResult ///< The host returned an unusable target.
};

struct DownloadResolution
{
    DownloadResolutionStatus status = DownloadResolutionStatus::Cancelled;
    DownloadTarget target;
    QString error;
};

using DownloadCompletion = std::function<void(DownloadResolution)>;

struct WebsiteDataResult
{
    bool success = true;
    QString error;
};

struct WebViewHostCallbacks
{
    /// Receives main-frame navigation state changes.
    std::function<void(const LoadEvent&)> onLoad;
    /// Opens a URL after the policy returns NavigationDecision::OpenExternally.
    std::function<void(const QUrl&)> onOpenExternal;
    /// Takes ownership of an allowed popup. Retain the WebViewPtr before returning.
    std::function<void(const NewWindowRequest&, WebViewPtr)> onNewWindow;
    /// Must invoke the completion exactly once; an empty callback cancels selection.
    std::function<void(const FileSelectionRequest&, FileSelectionCompletion)> onSelectFiles;
    /// Must invoke the completion exactly once; an empty callback cancels download.
    std::function<void(const DownloadRequest&, DownloadCompletion)> onResolveDownload;
};

enum class BridgeMessageKind : int
{
    Event,   ///< Fire-and-forget notification; requestId and error are empty.
    Request, ///< Request expecting one response with the same requestId.
    Response ///< Success payload or error for an earlier request.
};

struct BridgeMessage
{
    int version = 1;
    BridgeMessageKind kind = BridgeMessageKind::Event;
    QString type;
    QString requestId;
    QJsonObject payload;
    QString error;
};

class BridgeTransport
{
public:
    virtual ~BridgeTransport() = default;
    /// Returns false when the message could not be handed to the page.
    virtual bool send(const QByteArray& message) = 0;
    /// Permanently disconnects this transport.
    virtual void invalidate() = 0;
};

/// Structured, schema-validated messaging between the current page and C++.
class SYSTEM_WEBVIEW_API WebViewBridge final
{
public:
    using RequestCompletion = std::function<void(const QJsonObject&, const QString&)>;
    using EventHandler = std::function<void(const QJsonObject&)>;
    using Reply = std::function<void(const QJsonObject&, const QString&)>;
    using RequestHandler = std::function<void(const QJsonObject&, Reply)>;
    using Validator = std::function<bool(const BridgeMessage&, bool, int, QString*)>;

    explicit WebViewBridge(std::unique_ptr<BridgeTransport> transport = { });
    ~WebViewBridge();

    WebViewBridge(const WebViewBridge&) = delete;
    WebViewBridge& operator=(const WebViewBridge&) = delete;

    /// Sends a fire-and-forget event to the page.
    void emitEvent(const QString& type, const QJsonObject& payload = { });
    /// Sends a request; pending completions are cancelled on navigation or close.
    void call(const QString& type, const QJsonObject& payload, RequestCompletion completion = { });
    /// Sets the sole page-event handler for type; pass an empty handler to remove it.
    void setEventHandler(const QString& type, EventHandler handler);
    /// Sets the sole request handler for type; pass empty to remove it. Replies once.
    void setRequestHandler(const QString& type, RequestHandler handler);
    void setValidator(Validator validator);
    void setTransport(std::unique_ptr<BridgeTransport> transport);
    void receive(const QByteArray& message, const QUrl& source = { });
    void cancelPending(const QString& error = QStringLiteral("Bridge request cancelled."));
    void invalidate();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct PublishedResource
{
    QString token;
    QUrl url;
    QString mimeType;
    qint64 size = -1;
};

struct ResourceRequest
{
    QUrl url;
    QUrl sourceOrigin;
    QString documentToken;
    QString rangeHeader;
    qint64 rangeStart = -1;
    qint64 rangeEnd = -1;
};

struct ResourceResponse
{
    int status = 404;
    QString mimeType;
    qint64 totalSize = 0;
    qint64 offset = 0;
    qint64 length = 0;
    std::shared_ptr<void> lease;
    std::unique_ptr<QIODevice> body;
    bool acceptRanges = false;
};

class WebResourceManager final
{
public:
    SYSTEM_WEBVIEW_API explicit WebResourceManager(QUrl origin = { });
    SYSTEM_WEBVIEW_API ~WebResourceManager();

    /// Publishes an immutable snapshot of path to the current document.
    /// An empty token indicates failure. Release the token when the page is done.
    SYSTEM_WEBVIEW_API PublishedResource
        publishFile(const QString& path, const QString& mimeType = { }, const QString& documentToken = { });
    /// Resolves a published URL. Intended for platform backends and tests.
    SYSTEM_WEBVIEW_API ResourceResponse open(const ResourceRequest& request) const;
    SYSTEM_WEBVIEW_API void release(const QString& token);

private:
    friend class WebViewState;
    void setRevocationHandler(std::function<void(const QString&)> handler);
    void setContext(const QUrl& resourceOrigin, const QUrl& documentOrigin, const QString& documentToken);
    void setDocumentToken(const QString& documentToken);
    void releaseForDocument(const QString& documentToken);
    void releaseAll();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

class IWebView
{
public:
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebView() = default;
    /// Qt container to insert into the final widget layout. The view retains it.
    virtual QWidget* widget() = 0;
    virtual InitializationState initializationState() const = 0;
    /// Runs once when initialization reaches Ready, Failed, or Closed.
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    /// Attaches the native view after widget() has its final geometry.
    virtual void attachNativeView() = 0;
    /// Detaches the native view before its Qt host is destroyed or reparented.
    virtual void detachNativeView() = 0;
    /// Opens a session-registered application at an optional application route.
    virtual void open(WebApplicationPtr application, const QString& route = { }) = 0;
    /// Navigates directly; WebViewPolicy still decides whether the URL is allowed.
    virtual void navigate(const QUrl& url) = 0;
    /// Loads an HTML string. Set baseUrl when the document uses relative URLs.
    virtual void loadDocument(const QString& html, const QUrl& baseUrl = { }) = 0;
    virtual void stop() = 0;
    virtual void reload() = 0;
    /// Idempotently closes the native view and cancels pending callbacks.
    virtual void close() = 0;
    virtual bool isClosed() const = 0;
    virtual WebViewBridge& bridge() = 0;
    virtual WebResourceManager& resources() = 0;
    virtual void setHostCallbacks(WebViewHostCallbacks callbacks) = 0;
};

class IWebViewSession
{
public:
    using ClearCompletion = std::function<void(const WebsiteDataResult&)>;
    using InitializationCompletion = std::function<void(const InitializationResult&)>;

    virtual ~IWebViewSession() = default;
    virtual InitializationState initializationState() const = 0;
    /// Runs once when initialization reaches Ready, Failed, or Closed.
    virtual void whenInitialized(InitializationCompletion completion) = 0;
    /// Validates and registers an application. Returns null for invalid/duplicate options.
    virtual WebApplicationPtr createApplication(WebApplicationOptions options) = 0;
    /// Creates a view owned by the caller and logically owned by this session.
    virtual WebViewPtr createWebView(QWidget* parent = nullptr) = 0;
    /// Clear operations are asynchronous; omitted completions discard the result.
    virtual void clearCache(ClearCompletion completion = { }) = 0;
    virtual void clearCookies(ClearCompletion completion = { }) = 0;
    virtual void clearWebsiteData(ClearCompletion completion = { }) = 0;
    /// Query runtime support instead of inferring it from the operating system.
    virtual bool supports(WebViewCapability capability) const = 0;
};

/// Owns session state shared by its applications and views.
using WebViewSessionPtr = std::unique_ptr<IWebViewSession>;

struct BridgeMessageSchema
{
    QHash<QString, QJsonValue::Type> requiredPayloadFields;
    bool allowAdditionalPayloadFields = false;
};

struct WebViewPolicyConfig
{
    /// app:// hosts that may be loaded and granted bridge access.
    QSet<QString> allowedAppHosts;
    /// Canonical roots from which file:// navigation is allowed.
    QStringList allowedFileRoots;
    /// Exact normalized HTTP(S) origins trusted during local development.
    QSet<QString> trustedDevelopmentOrigins;
    /// Exact normalized HTTPS origins allowed to use the bridge.
    QSet<QString> trustedHttpsOrigins;
    /// Allowed message types and payload shapes in each direction.
    QHash<QString, BridgeMessageSchema> pageToHostSchemas;
    QHash<QString, BridgeMessageSchema> hostToPageSchemas;
    /// Maximum serialized bridge message size in either direction.
    int maximumBridgeMessageBytes = 64 * 1024;
};

/// Security and user-interaction decisions shared by every view in a session.
class SYSTEM_WEBVIEW_API WebViewPolicy
{
public:
    explicit WebViewPolicy(WebViewPolicyConfig config = { });
    WebViewPolicy(const WebViewPolicy& other);
    WebViewPolicy& operator=(const WebViewPolicy& other);
    virtual ~WebViewPolicy();

    virtual NavigationDecision decideNavigation(const NavigationRequest& request) const;
    virtual NewWindowDecision decideNewWindow(const NewWindowRequest& request) const;
    virtual bool allowsBridge(const QUrl& committedUrl) const;
    virtual bool validatePageToHostMessage(const BridgeMessage& message, QString* error = nullptr) const;
    virtual bool validateHostToPageMessage(const BridgeMessage& message, QString* error = nullptr) const;
    int maximumBridgeMessageBytes() const;
    virtual PermissionDecision decidePermission(const PermissionRequest& request) const;
    virtual DownloadDecision decideDownload(const DownloadRequest& request) const;

protected:
    const WebViewPolicyConfig& config() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

using WebViewPolicyPtr = std::shared_ptr<const WebViewPolicy>;

/// Creates the deny-by-default policy configured by config.
SYSTEM_WEBVIEW_API WebViewPolicyPtr createDefaultWebViewPolicy(WebViewPolicyConfig config = { });
/// Creates a platform session. Keep it alive until all of its views are destroyed.
SYSTEM_WEBVIEW_API WebViewSessionPtr createWebViewSession(WebViewSessionOptions options, WebViewPolicyPtr policy = { });
} // namespace webview
