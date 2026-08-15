#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>
#include <memory>

namespace webview
{
class IWebView;
using WebViewPtr = std::unique_ptr<IWebView>;

enum class LoadState { Started, Redirected, Committed, Finished, Failed };
enum class InitializationState { Initializing, Ready, Failed, Closed };

struct InitializationResult {
    InitializationState state = InitializationState::Ready;
    QString error;
};

struct LoadEvent {
    LoadState state = LoadState::Started;
    QUrl url;
    QString error;
    quint64 navigationId = 0;
    bool isMainFrame = true;
};

enum class NavigationDecision { Allow, Cancel, OpenExternally };

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

enum class NewWindowDecision { Allow, Cancel };
enum class PermissionDecision { Allow, Deny, Unsupported };
enum class PermissionKind { Camera, Microphone, Location, Notifications, Clipboard, FilePicker };
enum class DownloadDecision { Allow, Cancel };
enum class CapabilitySupport { Supported, Unsupported };
enum class WebViewCapability {
    PersistentProfile,
    PrivateProfile,
    FileSelection,
    DownloadDefault,
    DownloadTarget,
    ResourceMapping,
    Camera,
    Microphone,
    Location,
    Notifications,
    Clipboard
};

enum class SessionMode { Persistent, Ephemeral };

struct WebResourceMapping {
    QUrl origin;
    QString localDirectory;
};

struct WebViewSessionOptions {
    SessionMode mode = SessionMode::Ephemeral;
    QString profilePath;
    QVector<WebResourceMapping> resourceMappings;
};

struct PermissionRequest {
    PermissionKind kind = PermissionKind::Camera;
    QUrl origin;
};

struct DownloadRequest {
    QUrl url;
    QUrl origin;
    QUrl documentUrl;
    QString suggestedFileName;
};

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

struct BridgeMessage {
    int version = 1;
    QString type;
    QJsonObject payload;
};

enum class MessageError { None, Closed, NavigationChanged, Rejected, Unsupported };

struct MessageResult {
    MessageError error = MessageError::None;
    QString detail;
};

struct WebsiteDataResult {
    bool success = true;
    QString error;
};

struct WebViewHostCallbacks {
    std::function<void(const LoadEvent&)> load;
    std::function<void(const QUrl&)> openExternal;
    std::function<void(const NewWindowRequest&, WebViewPtr)> newWindow;
    std::function<void(const BridgeMessage&)> message;
    std::function<void(const FileSelectionRequest&, FileSelectionCompletion)> selectFiles;
    std::function<void(const DownloadRequest&, DownloadCompletion)> resolveDownload;
};
} // namespace webview
