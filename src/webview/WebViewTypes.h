#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace webview
{
class IWebView;
using WebViewPtr = std::unique_ptr<IWebView>;

enum class LoadState { Started, Redirected, Committed, Finished, Failed };

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

struct PermissionRequest {
    PermissionKind kind = PermissionKind::Camera;
    QUrl origin;
};

struct DownloadRequest {
    QUrl url;
    QUrl origin;
    QString suggestedFileName;
};

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
};
} // namespace webview
