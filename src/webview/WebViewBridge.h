#pragma once

#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QUrl>
#include <QHash>

#include <functional>
#include <memory>

namespace webview
{
enum class BridgeMessageKind { Event, Request, Response };

struct BridgeMessage {
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
    virtual bool send(const QByteArray& message) = 0;
    virtual void invalidate() = 0;
};

class WebViewBridge final
{
public:
    using RequestCompletion = std::function<void(const QJsonObject&, const QString&)>;
    using EventHandler = std::function<void(const QJsonObject&)>;
    using Reply = std::function<void(const QJsonObject&, const QString&)>;
    using RequestHandler = std::function<void(const QJsonObject&, Reply)>;
    using Validator = std::function<bool(const BridgeMessage&, bool, int, QString*)>;

    explicit WebViewBridge(std::unique_ptr<BridgeTransport> transport = {});
    ~WebViewBridge();

    WebViewBridge(const WebViewBridge&) = delete;
    WebViewBridge& operator=(const WebViewBridge&) = delete;

    void emitEvent(const QString& type, const QJsonObject& payload = {});
    void call(const QString& type, const QJsonObject& payload, RequestCompletion completion = {});
    void on(const QString& type, EventHandler handler);
    void onRequest(const QString& type, RequestHandler handler);
    void setValidator(Validator validator);
    void setTransport(std::unique_ptr<BridgeTransport> transport);
    void receive(const QByteArray& message, const QUrl& source = {});
    void cancelPending(const QString& error = QStringLiteral("Bridge request cancelled."));
    void invalidate();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace webview
