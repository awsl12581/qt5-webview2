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
 * appOptions.id = "main";
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

#include <QIODevice>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

class QWidget;

namespace webview
{
// All standard strings in this API contain UTF-8 text (including filesystem paths).
// BridgeTransport strings contain length-delimited UTF-8 JSON bytes.
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
    std::string error;
};

struct LoadEvent
{
    LoadState state = LoadState::Started;
    QUrl url;
    std::string error;
    std::uint64_t navigationId = 0;
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
    std::string directory;
    /// Document served when a route resolves to a directory.
    std::string entryDocument = "index.html";
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
    std::string id;
    ApplicationSource source;
    /// Bridge access still requires the committed origin to be trusted by policy.
    BridgeAccess bridgeAccess = BridgeAccess::Denied;
};

struct WebViewSessionOptions
{
    SessionMode mode = SessionMode::Ephemeral;
    /// User-data directory for a persistent WebView2 profile; ignored by WKWebView.
    std::string profilePath;
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
    std::string suggestedFileName;
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
    std::vector<std::string> paths;
    std::string error;
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
    std::string filePath;
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
    std::string error;
};

using DownloadCompletion = std::function<void(DownloadResolution)>;

struct WebsiteDataResult
{
    bool success = true;
    std::string error;
};

enum class RuntimeFailureKind : int
{
    WebContentProcessTerminated,   ///< The process rendering page content exited.
    WebContentProcessUnresponsive, ///< The process rendering page content stopped responding.
    BrowserProcessTerminated,      ///< The browser process shared by the session exited.
    AuxiliaryProcessTerminated,    ///< A GPU, utility, plugin, or other helper process exited.
    Unknown                        ///< The backend could not classify the failed process.
};

struct RuntimeFailureEvent
{
    RuntimeFailureKind kind = RuntimeFailureKind::Unknown;
    std::string reason;
    std::int64_t nativeCode = 0;
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
    /// Reports a browser runtime failure affecting this view.
    std::function<void(const RuntimeFailureEvent&)> onRuntimeFailure;
};

struct WebViewSessionHostCallbacks
{
    /// Reports a browser runtime failure affecting the session.
    std::function<void(const RuntimeFailureEvent&)> onRuntimeFailure;
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
    std::string type;
    std::string requestId;
    QJsonObject payload;
    std::string error;
};

class BridgeTransport
{
public:
    virtual ~BridgeTransport() = default;
    /// Returns false when the message could not be handed to the page.
    virtual bool send(const std::string& message) = 0;
    /// Permanently disconnects this transport.
    virtual void invalidate() = 0;
};

/// Structured, schema-validated messaging between the current page and C++.
class SYSTEM_WEBVIEW_API WebViewBridge final
{
public:
    using RequestCompletion = std::function<void(const QJsonObject&, const std::string&)>;
    using EventHandler = std::function<void(const QJsonObject&)>;
    using Reply = std::function<void(const QJsonObject&, const std::string&)>;
    using RequestHandler = std::function<void(const QJsonObject&, Reply)>;
    using Validator = std::function<bool(const BridgeMessage&, bool, int, std::string*)>;

    explicit WebViewBridge(std::unique_ptr<BridgeTransport> transport = { });
    ~WebViewBridge();

    WebViewBridge(const WebViewBridge&) = delete;
    WebViewBridge& operator=(const WebViewBridge&) = delete;

    /// Sends a fire-and-forget event to the page.
    void emitEvent(const std::string& type, const QJsonObject& payload = {});
    /// Sends a request; pending completions are cancelled on navigation or close.
    void call(const std::string& type, const QJsonObject& payload, RequestCompletion completion = {});
    /// Sets the sole page-event handler for type; pass an empty handler to remove it.
    void setEventHandler(const std::string& type, EventHandler handler);
    /// Sets the sole request handler for type; pass empty to remove it. Replies once.
    void setRequestHandler(const std::string& type, RequestHandler handler);
    void setValidator(Validator validator);
    void setTransport(std::unique_ptr<BridgeTransport> transport);
    void receive(const std::string& message, const QUrl& source = {});
    void cancelPending(const std::string& error = "Bridge request cancelled.");
    void invalidate();

private:
    friend class WebViewState;
    void setDiagnosticContext(std::uint64_t sessionId, std::uint64_t viewId);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct PublishedResource
{
    std::string token;
    QUrl url;
    std::string mimeType;
    std::int64_t size = -1;
};

struct ResourceRequest
{
    QUrl url;
    QUrl sourceOrigin;
    std::string documentToken;
    std::string rangeHeader;
    std::int64_t rangeStart = -1;
    std::int64_t rangeEnd = -1;
};

struct ResourceResponse
{
    int status = 404;
    std::string mimeType;
    std::int64_t totalSize = 0;
    std::int64_t offset = 0;
    std::int64_t length = 0;
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
        publishFile(const std::string& path, const std::string& mimeType = {}, const std::string& documentToken = {});
    /// Resolves a published URL. Intended for platform backends and tests.
    SYSTEM_WEBVIEW_API ResourceResponse open(const ResourceRequest& request) const;
    SYSTEM_WEBVIEW_API void release(const std::string& token);

private:
    friend class WebViewState;
    void setDiagnosticContext(std::uint64_t sessionId, std::uint64_t viewId);
    void setRevocationHandler(std::function<void(const std::string&)> handler);
    void setContext(const QUrl& resourceOrigin, const QUrl& documentOrigin, const std::string& documentToken);
    void setDocumentToken(const std::string& documentToken);
    void releaseForDocument(const std::string& documentToken);
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
    virtual void open(WebApplicationPtr application, const std::string& route = {}) = 0;
    /// Navigates directly; WebViewPolicy still decides whether the URL is allowed.
    virtual void navigate(const QUrl& url) = 0;
    /// Loads an HTML string. Set baseUrl when the document uses relative URLs.
    virtual void loadDocument(const std::string& html, const QUrl& baseUrl = {}) = 0;
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
    virtual void setHostCallbacks(WebViewSessionHostCallbacks callbacks) = 0;
    /// Query runtime support instead of inferring it from the operating system.
    virtual bool supports(WebViewCapability capability) const = 0;
};

/// Owns session state shared by its applications and views.
using WebViewSessionPtr = std::unique_ptr<IWebViewSession>;

struct BridgeMessageSchema
{
    std::unordered_map<std::string, QJsonValue::Type> requiredPayloadFields;
    bool allowAdditionalPayloadFields = false;
};

struct WebViewPolicyConfig
{
    /// app:// hosts that may be loaded and granted bridge access.
    std::unordered_set<std::string> allowedAppHosts;
    /// Canonical roots from which file:// navigation is allowed.
    std::vector<std::string> allowedFileRoots;
    /// Exact normalized HTTP(S) origins trusted during local development.
    std::unordered_set<std::string> trustedDevelopmentOrigins;
    /// Exact normalized HTTPS origins allowed to use the bridge.
    std::unordered_set<std::string> trustedHttpsOrigins;
    /// Allowed message types and payload shapes in each direction.
    std::unordered_map<std::string, BridgeMessageSchema> pageToHostSchemas;
    std::unordered_map<std::string, BridgeMessageSchema> hostToPageSchemas;
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
    virtual bool validatePageToHostMessage(const BridgeMessage& message, std::string* error = nullptr) const;
    virtual bool validateHostToPageMessage(const BridgeMessage& message, std::string* error = nullptr) const;
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
