#include "webview/WebViewBridge.h"
#include "internal/BridgePageScript.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

#include <QHash>

#include <utility>

namespace webview
{
QString bridgePageScript(
    const QString& documentTokenExpression,
    const QString& nativePostExpression,
    const QString& nativeReceiveRegistration,
    const QString& prelude)
{
    return QStringLiteral(R"JS((() => {
  %4
  const pending = new Map();
  const handlers = new Map();
  const send = message => %2({ documentToken: (%1), message });
  const transport = Object.freeze({
    postMessage(message) {
      if (!message || typeof message.type !== 'string' || !message.type) throw new TypeError('type must be a nonempty string');
      const payload = message.payload === undefined ? {} : message.payload;
      send({ version: 1, kind: 'event', type: message.type, requestId: '', payload, error: '' });
    },
    on(type, handler) {
      if (typeof handler !== 'function') throw new TypeError('handler must be a function');
      const listeners = handlers.get(type) || new Set();
      listeners.add(handler);
      handlers.set(type, listeners);
      return () => { listeners.delete(handler); if (!listeners.size) handlers.delete(type); };
    },
    call(type, payload = {}) {
      if (typeof type !== 'string' || !type) return Promise.reject(new TypeError('type must be a nonempty string'));
      const requestId = crypto.randomUUID ? crypto.randomUUID() : `${Date.now()}-${Math.random()}`;
      return new Promise((resolve, reject) => {
        pending.set(requestId, { resolve, reject });
        send({ version: 1, kind: 'request', type, requestId, payload, error: '' });
      });
    }
  });
  Object.defineProperty(window, 'systemWebView', {
    value: transport, configurable: false, enumerable: true, writable: false
  });
  window.__systemWebViewReceive = function(message) {
    if (message.kind === 'response') {
      const request = pending.get(message.requestId);
      if (!request) return;
      pending.delete(message.requestId);
      message.error ? request.reject(new Error(message.error)) : request.resolve(message.payload);
      return;
    }
    let replied = false;
    const detail = { ...message, reply(payload = {}, error = '') {
      if (message.kind !== 'request' || replied) return;
      replied = true;
      send({ version: 1, kind: 'response', type: message.type,
        requestId: message.requestId, payload, error });
    }};
    for (const handler of handlers.get(message.type) || []) handler(detail.payload, detail);
    window.dispatchEvent(new CustomEvent('system-webview-message', { detail }));
  };
  %3
})();)JS")
        .arg(documentTokenExpression, nativePostExpression, nativeReceiveRegistration, prelude);
}

namespace
{
QByteArray encode(const BridgeMessage& message)
{
    const QString kind = message.kind == BridgeMessageKind::Request    ? QStringLiteral("request")
                         : message.kind == BridgeMessageKind::Response ? QStringLiteral("response")
                                                                       : QStringLiteral("event");
    return QJsonDocument(
               QJsonObject { { QStringLiteral("version"), message.version },
                             { QStringLiteral("kind"), kind },
                             { QStringLiteral("type"), message.type },
                             { QStringLiteral("requestId"), message.requestId },
                             { QStringLiteral("payload"), message.payload },
                             { QStringLiteral("error"), message.error } })
        .toJson(QJsonDocument::Compact);
}

bool decode(const QByteArray& bytes, BridgeMessage* result)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const auto object = document.object();
    if (object.size() != 6) {
        return false;
    }
    const auto version = object.value(QStringLiteral("version"));
    const auto kind = object.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("event") && kind != QStringLiteral("request") && kind != QStringLiteral("response")) {
        return false;
    }
    if (!version.isDouble() || version.toInt(-1) != 1 || !object.value(QStringLiteral("kind")).isString()
        || !object.value(QStringLiteral("type")).isString() || !object.value(QStringLiteral("requestId")).isString()
        || !object.value(QStringLiteral("payload")).isObject() || !object.value(QStringLiteral("error")).isString()) {
        return false;
    }
    result->version = version.toInt();
    result->kind = kind == QStringLiteral("request")    ? BridgeMessageKind::Request
                   : kind == QStringLiteral("response") ? BridgeMessageKind::Response
                                                        : BridgeMessageKind::Event;
    result->type = object.value(QStringLiteral("type")).toString();
    result->requestId = object.value(QStringLiteral("requestId")).toString();
    result->payload = object.value(QStringLiteral("payload")).toObject();
    result->error = object.value(QStringLiteral("error")).toString();
    if (result->type.isEmpty()) {
        return false;
    }
    if (result->kind == BridgeMessageKind::Event) {
        return result->requestId.isEmpty() && result->error.isEmpty();
    }
    return !result->requestId.isEmpty() && (result->kind != BridgeMessageKind::Request || result->error.isEmpty());
}
}

class WebViewBridge::Impl
{
public:
    struct Pending
    {
        QString type;
        RequestCompletion completion;
    };

    std::unique_ptr<BridgeTransport> transport;
    QHash<QString, Pending> pending;
    QHash<QString, EventHandler> handlers;
    QHash<QString, RequestHandler> requestHandlers;
    Validator validator;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool valid = true;
};

WebViewBridge::WebViewBridge(std::unique_ptr<BridgeTransport> transport)
    : impl_(std::make_unique<Impl>())
{
    impl_->transport = std::move(transport);
}

WebViewBridge::~WebViewBridge()
{
    invalidate();
}

void WebViewBridge::emitEvent(const QString& type, const QJsonObject& payload)
{
    if (!impl_->valid || !impl_->transport) {
        return;
    }
    BridgeMessage message { 1, BridgeMessageKind::Event, type, { }, payload, { } };
    QString error;
    const auto bytes = encode(message);
    if (!impl_->validator || impl_->validator(message, true, bytes.size(), &error)) {
        impl_->transport->send(bytes);
    }
}

void WebViewBridge::call(const QString& type, const QJsonObject& payload, RequestCompletion completion)
{
    if (!impl_->valid) {
        if (completion) {
            completion({ }, QStringLiteral("Bridge is invalid."));
        }
        return;
    }
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool hasCompletion = static_cast<bool>(completion);
    if (hasCompletion) {
        impl_->pending.insert(requestId, { type, std::move(completion) });
    }
    if (!impl_->transport) {
        if (hasCompletion) {
            auto callback = impl_->pending.take(requestId);
            callback.completion({ }, QStringLiteral("Bridge transport is unavailable."));
        }
        return;
    }
    BridgeMessage message { 1, BridgeMessageKind::Request, type, requestId, payload, { } };
    QString error;
    const auto bytes = encode(message);
    if (impl_->validator && !impl_->validator(message, true, bytes.size(), &error)) {
        auto callback = impl_->pending.take(requestId);
        if (callback.completion) {
            callback.completion({ }, error);
        }
        return;
    }
    if (!impl_->transport->send(bytes)) {
        auto callback = impl_->pending.take(requestId);
        if (callback.completion) {
            callback.completion({ }, QStringLiteral("Bridge transport rejected the message."));
        }
    }
}

void WebViewBridge::on(const QString& type, EventHandler handler)
{
    if (handler) {
        impl_->handlers.insert(type, std::move(handler));
    }
    else {
        impl_->handlers.remove(type);
    }
}

void WebViewBridge::onRequest(const QString& type, RequestHandler handler)
{
    if (handler) {
        impl_->requestHandlers.insert(type, std::move(handler));
    }
    else {
        impl_->requestHandlers.remove(type);
    }
}

void WebViewBridge::setValidator(Validator validator)
{
    impl_->validator = std::move(validator);
}

void WebViewBridge::setTransport(std::unique_ptr<BridgeTransport> transport)
{
    if (impl_->transport) {
        impl_->transport->invalidate();
    }
    impl_->transport = std::move(transport);
}

void WebViewBridge::receive(const QByteArray& message, const QUrl&)
{
    if (!impl_->valid) {
        return;
    }
    BridgeMessage envelope;
    if (!decode(message, &envelope)) {
        return;
    }
    QString validationError;
    if (impl_->validator && !impl_->validator(envelope, false, message.size(), &validationError)) {
        return;
    }
    if (envelope.kind == BridgeMessageKind::Response) {
        const auto it = impl_->pending.find(envelope.requestId);
        if (it == impl_->pending.end() || it->type != envelope.type) {
            return;
        }
        auto callback = std::move(it->completion);
        impl_->pending.erase(it);
        if (callback) {
            callback(envelope.payload, envelope.error);
        }
        return;
    }
    if (envelope.kind == BridgeMessageKind::Request) {
        const auto handler = impl_->requestHandlers.value(envelope.type);
        if (!impl_->transport) {
            return;
        }
        const auto requestId = envelope.requestId;
        const auto type = envelope.type;
        const std::weak_ptr<bool> alive = impl_->alive;
        auto replied = std::make_shared<bool>(false);
        const auto reply = [this, requestId, type, replied, alive](const QJsonObject& payload, const QString& error) {
            const auto valid = alive.lock();
            if (*replied || !valid || !*valid || !impl_->transport) {
                return;
            }
            *replied = true;
            BridgeMessage response { 1, BridgeMessageKind::Response, type, requestId, payload, error };
            QString validationError;
            const auto bytes = encode(response);
            if (!impl_->validator || impl_->validator(response, true, bytes.size(), &validationError)) {
                impl_->transport->send(bytes);
            }
        };
        if (handler) {
            handler(envelope.payload, reply);
        }
        else {
            reply({ }, QStringLiteral("Unsupported bridge request."));
        }
        return;
    }
    const auto handler = impl_->handlers.value(envelope.type);
    if (handler) {
        handler(envelope.payload);
    }
}

void WebViewBridge::cancelPending(const QString& error)
{
    const auto pending = std::exchange(impl_->pending, { });
    for (const auto& callback : pending) {
        if (callback.completion) {
            callback.completion({ }, error);
        }
    }
}

void WebViewBridge::invalidate()
{
    if (!impl_->valid) {
        return;
    }
    impl_->valid = false;
    *impl_->alive = false;
    cancelPending(QStringLiteral("Bridge is invalid."));
    impl_->handlers.clear();
    impl_->requestHandlers.clear();
    if (impl_->transport) {
        impl_->transport->invalidate();
    }
}
} // namespace webview
