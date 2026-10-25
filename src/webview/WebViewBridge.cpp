#include "internal/BridgePageScript.h"
#include "internal/Diagnostics.h"
#include <system_webview/system_webview.h>

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

#include <cstdint>
#include <limits>
#include <unordered_map>

#include <utility>

namespace webview
{
std::string bridgePageScript(
    std::string_view documentTokenExpression,
    std::string_view nativePostExpression,
    std::string_view nativeReceiveRegistration,
    std::string_view prelude)
{
    constexpr std::string_view script = R"JS((() => {
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
})();)JS";
    const std::string_view substitutions[] = { documentTokenExpression, nativePostExpression, nativeReceiveRegistration, prelude };
    std::string result;
    result.reserve(
        script.size() + documentTokenExpression.size() + nativePostExpression.size() + nativeReceiveRegistration.size() + prelude.size());
    for (std::size_t index = 0; index < script.size(); ++index) {
        if (script[index] == '%' && index + 1 < script.size() && script[index + 1] >= '1' && script[index + 1] <= '4') {
            result.append(substitutions[script[++index] - '1']);
        }
        else {
            result.push_back(script[index]);
        }
    }
    return result;
}

namespace
{
std::string encode(const BridgeMessage& message)
{
    const QString kind = message.kind == BridgeMessageKind::Request    ? QStringLiteral("request")
                         : message.kind == BridgeMessageKind::Response ? QStringLiteral("response")
                                                                       : QStringLiteral("event");
    return QJsonDocument(
               QJsonObject { { QStringLiteral("version"), message.version },
                             { QStringLiteral("kind"), kind },
                             { QStringLiteral("type"), QString::fromStdString(message.type) },
                             { QStringLiteral("requestId"), QString::fromStdString(message.requestId) },
                             { QStringLiteral("payload"), message.payload },
                             { QStringLiteral("error"), QString::fromStdString(message.error) } })
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

bool decode(const std::string& bytes, BridgeMessage* result)
{
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray(bytes.data(), static_cast<int>(bytes.size())), &parseError);
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
    result->type = object.value(QStringLiteral("type")).toString().toStdString();
    result->requestId = object.value(QStringLiteral("requestId")).toString().toStdString();
    result->payload = object.value(QStringLiteral("payload")).toObject();
    result->error = object.value(QStringLiteral("error")).toString().toStdString();
    if (result->type.empty()) {
        return false;
    }
    if (result->kind == BridgeMessageKind::Event) {
        return result->requestId.empty() && result->error.empty();
    }
    return !result->requestId.empty() && (result->kind != BridgeMessageKind::Request || result->error.empty());
}
}

class WebViewBridge::Impl
{
public:
    struct Pending
    {
        std::string type;
        RequestCompletion completion;
    };

    std::unique_ptr<BridgeTransport> transport;
    std::unordered_map<std::string, Pending> pending;
    std::unordered_map<std::string, EventHandler> handlers;
    std::unordered_map<std::string, RequestHandler> requestHandlers;
    Validator validator;
    std::uint64_t sessionDiagnosticId = 0;
    std::uint64_t viewDiagnosticId = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool valid = true;

    Pending takePending(const std::string& requestId)
    {
        auto node = pending.extract(requestId);
        return node.empty() ? Pending {} : std::move(node.mapped());
    }
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

void WebViewBridge::emitEvent(const std::string& type, const QJsonObject& payload)
{
    if (!impl_->valid || !impl_->transport) {
        return;
    }
    BridgeMessage message { 1, BridgeMessageKind::Event, type, {}, payload, {} };
    std::string error;
    const auto bytes = encode(message);
    if (!impl_->validator || impl_->validator(message, true, bytes.size(), &error)) {
        if (!impl_->transport->send(bytes)) {
            qCWarning(systemWebViewBridge).noquote()
                << "event=bridge.transport_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        }
    }
}

void WebViewBridge::call(const std::string& type, const QJsonObject& payload, RequestCompletion completion)
{
    if (!impl_->valid) {
        if (completion) {
            completion({}, "Bridge is invalid.");
        }
        return;
    }
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const bool hasCompletion = static_cast<bool>(completion);
    if (hasCompletion) {
        impl_->pending.insert_or_assign(requestId, Impl::Pending { type, std::move(completion) });
    }
    if (!impl_->transport) {
        qCDebug(systemWebViewBridge).noquote() << "event=bridge.transport_unavailable" << "session=" << impl_->sessionDiagnosticId
                                               << "view=" << impl_->viewDiagnosticId;
        if (hasCompletion) {
            auto callback = impl_->takePending(requestId);
            callback.completion({}, "Bridge transport is unavailable.");
        }
        return;
    }
    BridgeMessage message { 1, BridgeMessageKind::Request, type, requestId, payload, {} };
    std::string error;
    const auto bytes = encode(message);
    if (impl_->validator && !impl_->validator(message, true, bytes.size(), &error)) {
        auto callback = impl_->takePending(requestId);
        if (callback.completion) {
            callback.completion({ }, error);
        }
        return;
    }
    if (!impl_->transport->send(bytes)) {
        qCWarning(systemWebViewBridge).noquote()
            << "event=bridge.transport_failed" << "session=" << impl_->sessionDiagnosticId << "view=" << impl_->viewDiagnosticId;
        auto callback = impl_->takePending(requestId);
        if (callback.completion) {
            callback.completion({}, "Bridge transport rejected the message.");
        }
    }
}

void WebViewBridge::setEventHandler(const std::string& type, EventHandler handler)
{
    if (handler) {
        impl_->handlers.insert_or_assign(type, std::move(handler));
    }
    else {
        impl_->handlers.erase(type);
    }
}

void WebViewBridge::setDiagnosticContext(std::uint64_t sessionId, std::uint64_t viewId)
{
    impl_->sessionDiagnosticId = sessionId;
    impl_->viewDiagnosticId = viewId;
}

void WebViewBridge::setRequestHandler(const std::string& type, RequestHandler handler)
{
    if (handler) {
        impl_->requestHandlers.insert_or_assign(type, std::move(handler));
    }
    else {
        impl_->requestHandlers.erase(type);
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

void WebViewBridge::receive(const std::string& message, const Url&)
{
    if (!impl_->valid) {
        return;
    }
    BridgeMessage envelope;
    if (!decode(message, &envelope)) {
        qCDebug(systemWebViewBridge).noquote() << "event=bridge.message_rejected" << "session=" << impl_->sessionDiagnosticId
                                               << "view=" << impl_->viewDiagnosticId << "reason=invalid_envelope";
        return;
    }
    std::string validationError;
    if (impl_->validator && !impl_->validator(envelope, false, message.size(), &validationError)) {
        return;
    }
    if (envelope.kind == BridgeMessageKind::Response) {
        const auto it = impl_->pending.find(envelope.requestId);
        if (it == impl_->pending.end() || it->second.type != envelope.type) {
            qCDebug(systemWebViewBridge).noquote() << "event=bridge.message_rejected" << "session=" << impl_->sessionDiagnosticId
                                                   << "view=" << impl_->viewDiagnosticId << "reason=unexpected_response";
            return;
        }
        auto callback = std::move(it->second.completion);
        impl_->pending.erase(it);
        if (callback) {
            callback(envelope.payload, envelope.error);
        }
        return;
    }
    if (envelope.kind == BridgeMessageKind::Request) {
        const auto found = impl_->requestHandlers.find(envelope.type);
        const auto handler = found == impl_->requestHandlers.end() ? RequestHandler {} : found->second;
        if (!impl_->transport) {
            return;
        }
        const auto requestId = envelope.requestId;
        const auto type = envelope.type;
        const std::weak_ptr<bool> alive = impl_->alive;
        auto replied = std::make_shared<bool>(false);
        const auto reply = [this, requestId, type, replied, alive](const QJsonObject& payload, const std::string& error) {
            const auto valid = alive.lock();
            if (*replied || !valid || !*valid || !impl_->transport) {
                return;
            }
            *replied = true;
            BridgeMessage response { 1, BridgeMessageKind::Response, type, requestId, payload, error };
            std::string validationError;
            const auto bytes = encode(response);
            if (!impl_->validator || impl_->validator(response, true, bytes.size(), &validationError)) {
                impl_->transport->send(bytes);
            }
        };
        if (handler) {
            handler(envelope.payload, reply);
        }
        else {
            reply({}, "Unsupported bridge request.");
        }
        return;
    }
    const auto found = impl_->handlers.find(envelope.type);
    const auto handler = found == impl_->handlers.end() ? EventHandler {} : found->second;
    if (handler) {
        handler(envelope.payload);
    }
}

void WebViewBridge::cancelPending(const std::string& error)
{
    const auto pending = std::exchange(impl_->pending, {});
    for (const auto& [requestId, callback] : pending) {
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
    cancelPending("Bridge is invalid.");
    impl_->handlers.clear();
    impl_->requestHandlers.clear();
    if (impl_->transport) {
        impl_->transport->invalidate();
    }
}
} // namespace webview
