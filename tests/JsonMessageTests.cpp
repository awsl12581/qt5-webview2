#include "internal/Application.h"
#include "internal/BridgePageScript.h"
#include "internal/Diagnostics.h"
#include "internal/ResourceMapping.h"
#include "internal/TextEncoding.h"
#include "webview/DocumentLifetime.h"
#include "webview/HostCompletion.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewState.h"
#include <system_webview/system_webview.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTemporaryDir>

#include <cassert>
#include <limits>
#include <string>
#include <vector>

namespace
{
std::vector<std::string> capturedMessages;

void captureMessage(QtMsgType, const QMessageLogContext& context, const QString& message)
{
    capturedMessages.push_back(std::string(context.category) + "|" + message.toStdString());
}

class MemoryBridgeTransport final : public webview::BridgeTransport
{
public:
    bool send(const std::string& message) override
    {
        sent.push_back(message);
        return true;
    }

    void invalidate() override { invalidated = true; }

    std::vector<std::string> sent;
    bool invalidated = false;
};

void testJsonMigrationBoundaries()
{
    using webview::Json;
    Json parsed;
    std::string error;
    assert(webview::BridgeMessage().payload.is_object());
    assert(webview::parseMessage("{}", &parsed, &error) && parsed.is_object() && parsed.empty());
    for (const std::string text :
         { "null", "[]", "true", "{", "{} trailing", "{\"x\":\"\xFF\"}", "{\"x\":\"\\uD800\"}", "{\"x\":1e999}" }) {
        assert(!webview::parseMessage(text, &parsed, &error));
        assert(!error.empty());
    }
    assert(!webview::parseMessage(std::string("{}\0trailing", 11), &parsed));
    assert(webview::parseMessage("{\"x\":1,\"x\":2}", &parsed));
    assert(parsed.at("x") == 2);
    const Json nulObject { { "text", std::string("a\0b", 3) } };
    const auto nulBytes = webview::serializeJson(nulObject);
    assert(nulBytes && nulBytes->find("\\u0000") != std::string::npos);
    assert(webview::parseMessage(*nulBytes, &parsed) && parsed == nulObject);
    assert(!webview::serializeJson(Json { { "x", std::string("\xFF") } }));
    assert(!webview::serializeJson(Json { { "x", std::numeric_limits<double>::quiet_NaN() } }));
    assert(!webview::serializeJson(Json { { "x", std::numeric_limits<double>::infinity() } }));
    assert(webview::serializeJson(Json("\"\\\n中文🌍")).value() == Json("\"\\\n中文🌍").dump());

    Json nested = Json::object();
    for (int depth = 0; depth < 120; ++depth) {
        nested = Json { { "child", std::move(nested) } };
    }
    const auto nestedBytes = webview::serializeJson(nested);
    assert(nestedBytes && webview::parseMessage(*nestedBytes, &parsed));
    for (int depth = 0; depth < 12; ++depth) {
        nested = Json { { "child", std::move(nested) } };
    }
    assert(!webview::serializeJson(nested));
    assert(!webview::parseMessage(nested.dump(), &parsed));

    webview::WebViewPolicyConfig config;
    config.pageToHostSchemas["number"] = { { { "value", webview::JsonType::Number } } };
    webview::WebViewPolicy policy(std::move(config));
    for (const Json value : { Json(-1), Json(std::uint64_t(18'446'744'073'709'551'615ULL)), Json(1.25) }) {
        assert(policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, "number", {}, Json { { "value", value } }, {} }));
    }
    assert(!policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, "number", {}, Json { { "value", nullptr } }, {} }));
    assert(!policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, "number", {}, nullptr, {} }));
    assert(webview::matchesJsonType(nullptr, webview::JsonType::Null));
    assert(webview::matchesJsonType(true, webview::JsonType::Boolean));
    assert(webview::matchesJsonType(Json::array(), webview::JsonType::Array));
    assert(webview::jsonString(Json::object(), "missing").empty());
    assert(webview::jsonString(Json { { "text", 1 } }, "text").empty());

    auto transport = std::make_unique<MemoryBridgeTransport>();
    auto* probe = transport.get();
    webview::WebViewBridge bridge(std::move(transport));
    int received = 0;
    bridge.setEventHandler("test", [&](const Json&) { ++received; });
    Json envelope { { "version", 1 },    { "kind", "event" },           { "type", "test" },
                    { "requestId", "" }, { "payload", Json::object() }, { "error", "" } };
    for (const Json version : { Json(1), Json(1.0) }) {
        envelope["version"] = version;
        bridge.receive(envelope.dump());
    }
    assert(received == 2);
    for (const Json version : { Json(1.5), Json("1"), Json(nullptr), Json(true) }) {
        envelope["version"] = version;
        bridge.receive(envelope.dump());
    }
    envelope["version"] = 1;
    for (const Json payload : { Json(nullptr), Json::array(), Json("text") }) {
        envelope["payload"] = payload;
        bridge.receive(envelope.dump());
    }
    assert(received == 2);
    bridge.emitEvent("test");
    assert(Json::parse(probe->sent.back()).at("payload").is_object());
    const auto sentCount = probe->sent.size();
    int failures = 0;
    for (const Json payload :
         { Json(nullptr), Json { { "x", std::string("\xFF") } }, Json { { "x", std::numeric_limits<double>::infinity() } } }) {
        bridge.call("test", payload, [&](const Json& result, const std::string& detail) {
            assert(result.is_object() && !detail.empty());
            ++failures;
        });
    }
    assert(failures == 3 && probe->sent.size() == sentCount);
    bridge.cancelPending();
    assert(failures == 3);
    bridge.setRequestHandler("test", [](const Json&, webview::WebViewBridge::Reply reply) {
        reply(Json { { "bad", std::numeric_limits<double>::quiet_NaN() } }, "");
        reply(Json::object(), "second reply");
    });
    envelope["kind"] = "request";
    envelope["requestId"] = "request-encoding-failure";
    envelope["payload"] = Json::object();
    bridge.receive(envelope.dump());
    assert(probe->sent.size() == sentCount + 1);
    const auto response = Json::parse(probe->sent.back());
    assert(response.at("kind") == "response" && response.at("payload").is_object());
    assert(!response.at("error").get<std::string>().empty());
}
}

int main()
{
    testJsonMigrationBoundaries();
    for (const std::string text : { "", "ASCII", "中文", "🌍", "\U00020000", "a中文🌍z" }) {
        const auto utf16 = webview::utf8ToUtf16(text);
        assert(utf16);
        const auto utf8 = webview::utf16ToUtf8(*utf16);
        assert(utf8 && *utf8 == text);
    }
    assert(webview::utf8ToUtf16("中文")->size() == 2);
    assert(webview::utf8ToUtf16("🌍")->size() == 2);
    assert(webview::utf16ToUtf8(u"🌍")->size() == 4);
    const std::string embeddedNul("a\0🌍", 6);
    const auto nulUtf16 = webview::utf8ToUtf16(embeddedNul);
    assert(nulUtf16 && nulUtf16->size() == 4 && (*nulUtf16)[1] == 0);
    assert(webview::utf16ToUtf8(*nulUtf16) == embeddedNul);
    for (const std::string text : { "\xFF", "\x80", "\xE4\xB8", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80" }) {
        assert(!webview::utf8ToUtf16(text));
        assert(!webview::utf8ToUtf16("valid prefix" + text));
    }
    for (const auto text : { std::u16string { 0xD800 },
                             std::u16string { 0xDC00 },
                             std::u16string { u'a', 0xD800, u'b' },
                             std::u16string { 0xDC00, 0xD800 } }) {
        assert(!webview::utf16ToUtf8(text));
    }
#ifdef _WIN32
    const auto wide = webview::utf8ToWide("中文🌍");
    assert(wide && wide->size() == 4);
    assert(webview::wideToUtf8(*wide) == "中文🌍");
    assert(webview::utf8ToWide("") == L"");
    assert(webview::wideToUtf8(L"") == "");
    assert(!webview::utf8ToWide(embeddedNul));
    assert(!webview::wideToUtf8(std::wstring(L"a\0b", 3)));
    assert(!webview::utf8ToWide("\xFF"));
    assert(!webview::wideToUtf8(std::wstring { static_cast<wchar_t>(0xD800) }));
#endif
    const webview::Json unicodeMessage { { "文本", "你好🌍" } };
    const auto jsonText = webview::serializeJson(unicodeMessage).value();
    const auto framedJson = "prefix" + jsonText + "suffix";
    webview::Json parsedMessage;
    std::string parseError;
    assert(webview::parseMessage(std::string_view(framedJson).substr(6, jsonText.size()), &parsedMessage, &parseError));
    assert(parsedMessage == unicodeMessage);
    assert(!webview::parseMessage("[]", &parsedMessage, &parseError));
    assert(!parseError.empty());

    const auto pageScript = webview::bridgePageScript("'%2-文档'", "native.send", "native.listen();", "const label = '%1';");
    assert(pageScript.find("const send = message => native.send({ documentToken: ('%2-文档'), message });") != std::string::npos);
    assert(pageScript.find("const label = '%1';") != std::string::npos);
    assert(pageScript.find("native.listen();") != std::string::npos);

    const auto previousHandler = qInstallMessageHandler(captureMessage);
    QLoggingCategory::setFilterRules(QStringLiteral("system_webview.*.debug=true\nsystem_webview.*.info=true"));
    {
        webview::WebViewState diagnosticState(webview::DiagnosticScope::View, 41);
        bool runtimeFailureReceived = false;
        diagnosticState.callbacks.onRuntimeFailure = [&](const webview::RuntimeFailureEvent& event) {
            runtimeFailureReceived = event.kind == webview::RuntimeFailureKind::WebContentProcessTerminated;
        };
        diagnosticState.markReady();
        diagnosticState.markReady();
        diagnosticState.emitLoad(
            webview::LoadState::Failed,
            7,
            webview::Url("https://user:password@example.com/private?token=secret#fragment"),
            "failure with /private/path");
        diagnosticState.emitRuntimeFailure({ webview::RuntimeFailureKind::WebContentProcessTerminated, "terminated", 9 });
        assert(runtimeFailureReceived);
        runtimeFailureReceived = false;
        diagnosticState.close();
        diagnosticState.emitRuntimeFailure({ webview::RuntimeFailureKind::WebContentProcessTerminated, "terminated", 9 });
        assert(!runtimeFailureReceived);
    }
    std::string diagnosticText;
    for (const auto& message : capturedMessages) {
        diagnosticText += message + "\n";
    }
    assert(diagnosticText.find("event=view.created") != std::string::npos);
    assert(diagnosticText.find("system_webview.lifecycle") != std::string::npos);
    assert(diagnosticText.find("event=view.ready") != std::string::npos);
    assert(diagnosticText.find("event=view.ready") == diagnosticText.rfind("event=view.ready"));
    assert(diagnosticText.find("event=navigation.failed") != std::string::npos);
    assert(diagnosticText.find("event=view.closed") != std::string::npos);
    assert(diagnosticText.find("session= 41") != std::string::npos);
    assert(diagnosticText.find("origin= https://example.com") != std::string::npos);
    assert(diagnosticText.find("password") == std::string::npos);
    assert(diagnosticText.find("private") == std::string::npos);
    assert(diagnosticText.find("secret") == std::string::npos);
    qInstallMessageHandler(previousHandler);
    QLoggingCategory::setFilterRules(QString());

    auto transport = std::make_unique<MemoryBridgeTransport>();
    auto* transportProbe = transport.get();
    webview::WebViewBridge bridge(std::move(transport));
    bool eventReceived = false;
    bridge.setEventHandler("notify", [&](const webview::Json& payload) { eventReceived = payload.at("value").get<int>() == 7; });
    bridge.receive((webview::Json { { "version", 1 },
                                    { "kind", "event" },
                                    { "type", "notify" },
                                    { "requestId", "" },
                                    { "payload", webview::Json { { "value", 7 } } },
                                    { "error", "" } })
                       .dump());
    assert(eventReceived);
    eventReceived = false;
    bridge.receive((webview::Json { { "kind", "event" },
                                    { "type", "notify" },
                                    { "requestId", "" },
                                    { "payload", webview::Json { { "value", 7 } } },
                                    { "error", "" } })
                       .dump());
    assert(!eventReceived);
    int unicodeEvents = 0;
    const std::string unicodeType = "通知🌍";
    bridge.setEventHandler(unicodeType, [&](const webview::Json&) { unicodeEvents = -1; });
    bridge.setEventHandler(unicodeType, [&](const webview::Json&) {
        ++unicodeEvents;
        bridge.setEventHandler(unicodeType, {});
    });
    const auto unicodeEvent = (webview::Json { { "version", 1 },
                                               { "kind", "event" },
                                               { "type", unicodeType },
                                               { "requestId", "" },
                                               { "payload", webview::Json::object() },
                                               { "error", "" } })
                                  .dump();
    bridge.receive(unicodeEvent);
    bridge.receive(unicodeEvent);
    assert(unicodeEvents == 1);
    bool callCompleted = false;
    bridge.call("查询🌍", { { "id", 9 } }, [&](const webview::Json& payload, const std::string& error) {
        callCompleted = error.empty() && payload.at("name").get<std::string>() == "item";
    });
    webview::Json request;
    request = webview::Json::parse(transportProbe->sent.back());
    assert(!request.empty());
    const auto requestId = request.at("requestId").get<std::string>();
    auto mismatchedResponse = request;
    mismatchedResponse["kind"] = "response";
    mismatchedResponse["type"] = "other";
    bridge.receive((mismatchedResponse).dump());
    assert(!callCompleted);
    bridge.receive((webview::Json { { "version", 1 },
                                    { "kind", "response" },
                                    { "type", "查询🌍" },
                                    { "requestId", requestId },
                                    { "payload", webview::Json { { "name", "item" } } },
                                    { "error", "" } })
                       .dump());
    assert(callCompleted);
    bridge.setRequestHandler("sum", [](const webview::Json& payload, webview::WebViewBridge::Reply reply) {
        reply({ { "result", payload.at("left").get<int>() + payload.at("right").get<int>() } }, {});
    });
    bridge.receive((webview::Json { { "version", 1 },
                                    { "kind", "request" },
                                    { "type", "sum" },
                                    { "requestId", "request-1" },
                                    { "payload", webview::Json { { "left", 2 }, { "right", 3 } } },
                                    { "error", "" } })
                       .dump());
    webview::Json response;
    response = webview::Json::parse(transportProbe->sent.back());
    assert(!response.empty());
    assert(response.at("kind").get<std::string>() == "response");
    assert(response.at("payload").at("result").get<int>() == 5);
    bool cancelled = false;
    bridge.call("wait", webview::Json::object(), [&](const webview::Json&, const std::string& error) { cancelled = !error.empty(); });
    bridge.cancelPending();
    assert(cancelled);
    int invalidationCompletions = 0;
    bridge.call("close", webview::Json::object(), [&](const webview::Json&, const std::string&) {
        ++invalidationCompletions;
        bridge.call("reentrant", webview::Json::object(), [&](const webview::Json&, const std::string&) { ++invalidationCompletions; });
    });
    bridge.invalidate();
    assert(transportProbe->invalidated);
    assert(invalidationCompletions == 2);

    QTemporaryDir files;
    assert(files.isValid());
    QFile allowedFile(files.filePath(QStringLiteral("index.html")));
    assert(allowedFile.open(QIODevice::WriteOnly));
    allowedFile.close();

    QFile sourceFile(files.filePath(QStringLiteral("数据🌍.bin")));
    assert(sourceFile.open(QIODevice::WriteOnly));
    assert(sourceFile.write("0123456789") == 10);
    sourceFile.close();
    webview::WebResourceManager unboundResources(webview::Url("app://ui"));
    assert(unboundResources.publishFile(sourceFile.fileName().toStdString()).token.empty());
    auto resourceState = std::make_shared<webview::WebViewState>();
    auto& resources = *resourceState->resources;
    const webview::Url documentOrigin("https://trusted.example");
    resourceState->setResourceContext(webview::Url("app://ui"), documentOrigin, "文档-1");
    assert(resources.publishFile(sourceFile.fileName().toStdString(), {}, "other-doc").token.empty());
    const auto published = resources.publishFile(sourceFile.fileName().toStdString());
    assert(!published.token.empty());
    assert(published.mimeType == "application/octet-stream");
    assert(published.url.toString().find(sourceFile.fileName().toStdString()) == std::string::npos);
    assert(sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(sourceFile.write("abcdefghij") == 10);
    sourceFile.close();
    auto rangeResponse = resources.open({ published.url, documentOrigin, "文档-1", "bytes=2-5" });
    assert(rangeResponse.status == 206 && rangeResponse.offset == 2 && rangeResponse.length == 4);
    assert(rangeResponse.body->readAll() == QByteArray("2345"));
    const auto snapshotPath = qobject_cast<QFile*>(rangeResponse.body.get())->fileName();
    assert(resources.open({ published.url, documentOrigin, "other-doc" }).status == 403);
    resources.release(published.token);
    assert(resources.open({ published.url, documentOrigin, "文档-1" }).status == 410);
    assert(QFileInfo::exists(snapshotPath));
    rangeResponse.body.reset();
    rangeResponse.lease.reset();
    assert(!QFileInfo::exists(snapshotPath));
    assert(QFileInfo::exists(sourceFile.fileName()));

    QFile emptyFile(files.filePath(QStringLiteral("empty.bin")));
    assert(emptyFile.open(QIODevice::WriteOnly));
    emptyFile.close();
    const auto emptyPublished = resources.publishFile(emptyFile.fileName().toStdString());
    assert(!emptyPublished.token.empty());
    const auto emptyResponse = resources.open({ emptyPublished.url, documentOrigin, "文档-1" });
    assert(emptyResponse.status == 200 && emptyResponse.length == 0);
    assert(resources.open({ emptyPublished.url, documentOrigin, "文档-1", "bytes=0-0" }).status == 416);

    webview::WebViewPolicyConfig resourcePolicyConfig;
    resourcePolicyConfig.trustedHttpsOrigins.insert(documentOrigin.toString());
    auto resourceTransport = std::make_unique<MemoryBridgeTransport>();
    auto* resourceTransportProbe = resourceTransport.get();
    resourceState->policy = std::make_shared<webview::WebViewPolicy>(std::move(resourcePolicyConfig));
    resourceState->committedUrl = webview::Url("https://trusted.example/index.html");
    resourceState->bridge->setTransport(std::move(resourceTransport));
    resourceState->bindBridgePolicy();
    const auto controlResource = resources.publishFile(sourceFile.fileName().toStdString());
    assert(!controlResource.token.empty());
    const auto releaseMessage = [&controlResource](const webview::Json& payload) {
        return (webview::Json { { "version", 1 },
                                { "kind", "event" },
                                { "type", "release-resource" },
                                { "requestId", "" },
                                { "payload", payload },
                                { "error", "" } })
            .dump();
    };
    resourceState->bridge->receive(releaseMessage({ { "token", controlResource.token }, { "extra", true } }));
    assert(resources.open({ controlResource.url, documentOrigin, "文档-1" }).status == 200);
    resourceState->bridge->receive(releaseMessage({ { "token", controlResource.token } }));
    assert(resources.open({ controlResource.url, documentOrigin, "文档-1" }).status == 410);
    assert(!resourceTransportProbe->sent.empty());
    const auto revokedEvent = webview::Json::parse(resourceTransportProbe->sent.back());
    assert(revokedEvent.at("type").get<std::string>() == "resource-revoked");
    assert(revokedEvent.at("payload").at("token").get<std::string>() == controlResource.token);

    webview::WebViewPolicyConfig config;
    config.allowedAppHosts.insert("ui");
    config.allowedFileRoots.push_back(files.path().toStdString());
    config.trustedDevelopmentOrigins.insert("http://127.0.0.1:5173");
    config.trustedHttpsOrigins.insert("https://trusted.example");
    config.pageToHostSchemas["ping"] = { { { "sequence", webview::JsonType::Number } } };
    config.hostToPageSchemas["pong"] = { { { "accepted", webview::JsonType::Boolean } } };
    config.maximumBridgeMessageBytes = 128;
    const webview::WebViewPolicy policy(std::move(config));
    webview::WebViewPolicy copiedPolicy(policy);
    webview::WebViewPolicy assignedPolicy;
    assignedPolicy = policy;
    assert(copiedPolicy.allowsBridge(webview::Url("app://ui/page")));
    assert(assignedPolicy.maximumBridgeMessageBytes() == 128);

    const auto navigation = [&policy](std::string_view url) {
        return policy.decideNavigation({ webview::Url(url), true, true, false });
    };
    assert(navigation("https://example.com/page") == webview::NavigationDecision::Allow);
    assert(navigation("http://127.0.0.1:5173/") == webview::NavigationDecision::Allow);
    assert(navigation("http://127.0.0.1:5174/") == webview::NavigationDecision::Cancel);
    assert(navigation("app://ui/home") == webview::NavigationDecision::Allow);
    assert(navigation(webview::Url::fromLocalFile(allowedFile.fileName().toStdString()).toString()) == webview::NavigationDecision::Allow);
    assert(navigation("app://other/home") == webview::NavigationDecision::Cancel);
    assert(navigation("javascript:alert(1)") == webview::NavigationDecision::Cancel);
    assert(navigation("custom://host/path") == webview::NavigationDecision::Cancel);
    assert(navigation("not a url") == webview::NavigationDecision::Cancel);

    assert(policy.decideNewWindow({ webview::Url("https://example.com"), true }) == webview::NewWindowDecision::Cancel);
    assert(policy.decidePermission({ webview::PermissionKind::Camera, webview::Url("app://ui") }) == webview::PermissionDecision::Deny);
    assert(policy.decideDownload({ webview::Url("https://example.com/file"), {}, {} }) == webview::DownloadDecision::Cancel);
    assert(policy.allowsBridge(webview::Url("app://ui/page")));
    assert(policy.allowsBridge(webview::Url("http://127.0.0.1:5173/page")));
    assert(policy.allowsBridge(webview::Url("https://trusted.example/path")));
    assert(!policy.allowsBridge(webview::Url("https://trusted.example.evil/path")));

    std::string validationError;
    const webview::BridgeMessage validMessage { 1, webview::BridgeMessageKind::Event, "ping", {}, webview::Json { { "sequence", 1 } }, {} };
    assert(policy.validatePageToHostMessage(validMessage, &validationError));
    assert(validationError.empty());
    assert(!policy.validateHostToPageMessage(validMessage, &validationError));
    assert(validationError.find("type") != std::string::npos);
    assert(policy.validateHostToPageMessage(
        { 1, webview::BridgeMessageKind::Event, "pong", {}, { { "accepted", true } }, {} },
        &validationError));
    assert(!policy.validatePageToHostMessage(
        { 2, webview::BridgeMessageKind::Event, validMessage.type, {}, validMessage.payload, {} },
        &validationError));
    assert(validationError.find("version") != std::string::npos);
    assert(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, "unknown", {}, validMessage.payload, {} },
        &validationError));
    assert(validationError.find("type") != std::string::npos);
    assert(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json::object(), {} },
        &validationError));
    assert(validationError.find("sequence") != std::string::npos);
    assert(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json { { "sequence", "wrong type" } }, {} },
        &validationError));
    assert(validationError.find("number") != std::string::npos);
    assert(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json { { "sequence", 1 }, { "extra", true } } },
        &validationError));
    assert(validationError.find("unexpected") != std::string::npos);
    assert(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json { { "sequence", std::string(200, 'x') } }, {} },
        &validationError));
    assert(validationError.find("size") != std::string::npos);

    webview::WebViewPolicyConfig unicodeConfig;
    unicodeConfig.pageToHostSchemas["查询🌍"] = { { { "名称", webview::JsonType::String } } };
    webview::WebViewPolicy unicodePolicy(std::move(unicodeConfig));
    webview::BridgeMessage unicodeEnvelope { 1, webview::BridgeMessageKind::Event, "查询🌍", {}, unicodeMessage, {} };
    assert(!unicodePolicy.validatePageToHostMessage(unicodeEnvelope, &validationError));
    assert(validationError == "Bridge message payload is missing required field: 名称");
    unicodeEnvelope.payload = { { "名称", "你好🌍" } };
    assert(unicodePolicy.validatePageToHostMessage(unicodeEnvelope, &validationError));
    assert(validationError.empty());

    webview::WebViewPolicyConfig wireLimitConfig;
    wireLimitConfig.allowedAppHosts.insert("ui");
    wireLimitConfig.pageToHostSchemas["notify"] = {};
    wireLimitConfig.maximumBridgeMessageBytes = 128;
    auto wireLimitState = std::make_shared<webview::WebViewState>();
    wireLimitState->policy = std::make_shared<webview::WebViewPolicy>(std::move(wireLimitConfig));
    wireLimitState->committedUrl = webview::Url("app://ui/index.html");
    wireLimitState->bindBridgePolicy();
    bool oversizedMessageReceived = false;
    wireLimitState->bridge->setEventHandler("notify", [&](const webview::Json&) { oversizedMessageReceived = true; });
    const std::string compactMessage = (webview::Json { { "version", 1 },
                                                        { "kind", "event" },
                                                        { "type", "notify" },
                                                        { "requestId", "" },
                                                        { "payload", webview::Json::object() },
                                                        { "error", "" } })
                                           .dump();
    wireLimitState->bridge->receive(compactMessage + std::string(128, ' '));
    assert(!oversizedMessageReceived);

    webview::DocumentLifetime lifetime;
    const auto firstDocument = lifetime.token();
    assert(lifetime.resultFor(firstDocument) == webview::DocumentError::None);
    lifetime.invalidate();
    assert(lifetime.resultFor(firstDocument) == webview::DocumentError::NavigationChanged);
    const auto secondDocument = lifetime.token();
    assert(lifetime.resultFor(secondDocument) == webview::DocumentError::None);
    lifetime.close();
    assert(lifetime.resultFor(secondDocument) == webview::DocumentError::Closed);
    lifetime.close();
    assert(lifetime.resultFor(lifetime.token()) == webview::DocumentError::Closed);

    auto state = std::make_shared<webview::WebViewState>();
    assert(state->initializationState() == webview::InitializationState::Initializing);
    bool queued = false;
    state->runWhenReady([&](const webview::InitializationResult& result) { queued = result.state == webview::InitializationState::Ready; });
    bool initialized = false;
    state->whenInitialized(
        [&](const webview::InitializationResult& result) { initialized = result.state == webview::InitializationState::Ready; });
    state->markReady();
    assert(initialized);
    assert(queued);
    assert(state->initializationState() == webview::InitializationState::Ready);

    auto failedState = std::make_shared<webview::WebViewState>();
    bool failed = false;
    failedState->whenInitialized([&](const webview::InitializationResult& result) {
        failed = result.state == webview::InitializationState::Failed && !result.error.empty();
    });
    failedState->failInitialization("test failure");
    assert(failed);

    bool queuedFailure = false;
    failedState->runWhenReady([&](const webview::InitializationResult& result) {
        queuedFailure = result.state == webview::InitializationState::Failed && result.error == "test failure";
    });
    assert(queuedFailure);

    auto closingState = std::make_shared<webview::WebViewState>();
    bool queuedClose = false;
    closingState->runWhenReady(
        [&](const webview::InitializationResult& result) { queuedClose = result.state == webview::InitializationState::Closed; });
    closingState->close();
    assert(queuedClose);

    webview::InitializationScheduler scheduler;
    std::vector<std::string> operationOrder;
    scheduler.runWhenReady([&](const webview::InitializationResult& result) {
        assert(result.state == webview::InitializationState::Ready);
        operationOrder.push_back("first");
        scheduler.runWhenReady([&](const webview::InitializationResult& nestedResult) {
            assert(nestedResult.state == webview::InitializationState::Ready);
            operationOrder.push_back("nested");
        });
    });
    scheduler.runWhenReady([&](const webview::InitializationResult&) { operationOrder.push_back("second"); });
    scheduler.markReady();
    assert(operationOrder == std::vector<std::string>({ "first", "second", "nested" }));

    auto completionState = std::make_shared<webview::WebViewState>();
    webview::HostCompletionGuard completionGuard(completionState);
    assert(completionGuard.claim().claim == webview::HostCompletionClaim::Accepted);
    assert(completionGuard.claim().claim == webview::HostCompletionClaim::Duplicate);

    webview::HostCompletionGuard closedGuard(completionState);
    completionState->close();
    assert(closedGuard.claim().claim == webview::HostCompletionClaim::OwnerUnavailable);
    webview::HostCompletionGuard destroyedGuard(completionState);
    completionState.reset();
    assert(destroyedGuard.claim().claim == webview::HostCompletionClaim::OwnerUnavailable);

    QTemporaryDir selectedRoot;
    assert(selectedRoot.isValid());
    const auto selectedFile = selectedRoot.filePath(QStringLiteral("selected.txt"));
    QFile file(selectedFile);
    assert(file.open(QIODevice::WriteOnly));
    file.close();
    QFile indexFile(selectedRoot.filePath(QStringLiteral("index.html")));
    assert(indexFile.open(QIODevice::WriteOnly));
    indexFile.close();
    const webview::FileSelectionRequest singleFileRequest { {}, {}, false, false };
    const auto selected = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile.toStdString() }, {} });
    assert(selected.status == webview::FileSelectionStatus::Selected);
    const auto empty = webview::normalizeFileSelectionResult(singleFileRequest, { webview::FileSelectionStatus::Selected, {}, {} });
    assert(empty.status == webview::FileSelectionStatus::Cancelled);
    const auto multiple = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile.toStdString(), selectedFile.toStdString() }, {} });
    assert(multiple.status == webview::FileSelectionStatus::InvalidResult);
    const auto directory = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedRoot.path().toStdString() }, {} });
    assert(directory.status == webview::FileSelectionStatus::InvalidResult);

    std::vector<webview::ResourceMapping> mappings {
        { webview::Url("app://demo"), selectedRoot.path().toStdString(), "index.html", true }
    };
    std::string mappingError;
    assert(webview::validateResourceMappings(&mappings, &mappingError));
    assert(mappings.front().localDirectory == QFileInfo(selectedRoot.path()).canonicalFilePath().toStdString());
    const auto* mapping = webview::findResourceMapping(mappings, webview::Url("app://demo/selected.txt"));
    assert(mapping);
    assert(
        webview::resolveMappedResource(*mapping, webview::Url("app://demo/selected.txt"), &mappingError)
        == QFileInfo(selectedFile).canonicalFilePath().toStdString());
    assert(webview::resolveMappedResource(*mapping, webview::Url("app://demo/%2e%2e/secret"), &mappingError).empty());
    assert(webview::resolveMappedResource(*mapping, webview::Url("app://demo/missing.txt"), &mappingError).empty());
    assert(
        webview::resolveMappedResource(*mapping, webview::Url("app://demo/orders/42"), &mappingError, true)
        == QFileInfo(indexFile.fileName()).canonicalFilePath().toStdString());
    assert(webview::resolveMappedResource(*mapping, webview::Url("app://demo/missing.js"), &mappingError, true).empty());
    const auto deniedNetworkPolicy = webview::localBundleContentSecurityPolicy(webview::ExternalNetworkAccess::Denied);
    assert(deniedNetworkPolicy.find("default-src 'self'") != std::string::npos);
    assert(deniedNetworkPolicy.find("connect-src 'self'") != std::string::npos);
    assert(deniedNetworkPolicy.find("form-action 'self'") != std::string::npos);
    assert(webview::localBundleContentSecurityPolicy(webview::ExternalNetworkAccess::Allowed).empty());
    QTemporaryDir outsideRoot;
    assert(outsideRoot.isValid());
    const auto outsideFile = outsideRoot.filePath(QStringLiteral("secret.txt"));
    QFile secret(outsideFile);
    assert(secret.open(QIODevice::WriteOnly));
    secret.close();
    const auto linkedFile = selectedRoot.filePath(QStringLiteral("linked.txt"));
    assert(QFile::link(outsideFile, linkedFile));
    if (QFileInfo(linkedFile).isSymLink()) {
        assert(webview::resolveMappedResource(*mapping, webview::Url("app://demo/linked.txt"), &mappingError).empty());
    }

    std::vector<webview::ResourceMapping> duplicateMappings { { webview::Url("app://demo"), selectedRoot.path().toStdString() },
                                                              { webview::Url("APP://DEMO"), selectedRoot.path().toStdString() } };
    assert(webview::validateResourceMappings(&duplicateMappings, &mappingError));
    std::vector<webview::ResourceMapping> invalidMappings { { webview::Url("https://demo/path"), selectedRoot.path().toStdString() } };
    assert(!webview::validateResourceMappings(&invalidMappings, &mappingError));

    const auto createBundleWithId = [&selectedRoot](const std::string& id) {
        webview::WebApplicationOptions options;
        options.id = id;
        options.source = webview::LocalBundle { selectedRoot.path().toStdString() };
        return webview::createApplication(std::move(options), nullptr);
    };
    for (const std::string id : { "demo-42", "中文应用", "é", "١٢", "Ⅻ", "²", "\U00020000", "\U00010400", "---" }) {
        const auto application = createBundleWithId(id);
        assert(application && application->id() == id);
        const auto origin = application->origin();
        assert(origin.isValid() && !origin.host().empty());
        const auto route = application->urlForRoute("index.html");
        std::vector<webview::ResourceMapping> unicodeMappings { { origin, selectedRoot.path().toStdString() } };
        assert(webview::validateResourceMappings(&unicodeMappings, &mappingError));
        const auto* unicodeMapping = webview::findResourceMapping(unicodeMappings, route);
        assert(unicodeMapping);
        assert(
            webview::resolveMappedResource(*unicodeMapping, route, &mappingError)
            == QFileInfo(indexFile).canonicalFilePath().toStdString());
        webview::WebViewPolicyConfig unicodeConfig;
        unicodeConfig.allowedAppHosts.insert(origin.host());
        const webview::WebViewPolicy unicodePolicy(std::move(unicodeConfig));
        assert(unicodePolicy.decideNavigation({ route }) == webview::NavigationDecision::Allow);
        assert(unicodePolicy.allowsBridge(route));
    }
    const std::vector<std::string> invalidIds { "",
                                                "a b",
                                                "a_b",
                                                "a/b",
                                                "🌍",
                                                "e\xCC\x81",
                                                "\xCC\x81",
                                                "\xFF",
                                                "\xE4\xB8",
                                                "\xC0\xAF",
                                                "\xED\xA0\x80",
                                                "\xF4\x90\x80\x80",
                                                std::string("a\0b", 3),
                                                std::string("\0", 1) };
    for (const auto& id : invalidIds) {
        assert(!createBundleWithId(id));
    }

    webview::WebApplicationOptions bundleOptions;
    bundleOptions.id = "demo-app";
    bundleOptions.source = webview::LocalBundle { selectedRoot.path().toStdString() };
    const auto bundleApplication = webview::createApplication(std::move(bundleOptions), &mappingError);
    assert(bundleApplication);
    assert(std::get<webview::LocalBundle>(bundleApplication->source()).externalNetworkAccess == webview::ExternalNetworkAccess::Denied);
    assert(bundleApplication->urlForRoute("orders/42") == webview::Url("app://demo-app/orders/42"));
    assert(bundleApplication->urlForRoute("订单/详情") == webview::Url("app://demo-app/订单/详情"));
    const auto unicodeFile = selectedRoot.filePath(QStringLiteral("数据.txt"));
    QFile unicodeResource(unicodeFile);
    assert(unicodeResource.open(QIODevice::WriteOnly));
    unicodeResource.close();
    assert(
        webview::resolveMappedResource(*mapping, webview::Url("app://demo/数据.txt"), &mappingError)
        == QFileInfo(unicodeFile).canonicalFilePath().toStdString());
    webview::WebApplicationOptions invalidNetworkAccess;
    invalidNetworkAccess.id = "invalid-network-access";
    invalidNetworkAccess.source =
        webview::LocalBundle { selectedRoot.path().toStdString(), "index.html", true, static_cast<webview::ExternalNetworkAccess>(-1) };
    assert(!webview::createApplication(std::move(invalidNetworkAccess), &mappingError));
    webview::WebApplicationOptions devOptions;
    devOptions.id = "demo-dev";
    devOptions.source = webview::DevelopmentServer { webview::Url("http://127.0.0.1:5173") };
    const auto devApplication = webview::createApplication(std::move(devOptions), &mappingError);
    assert(devApplication && devApplication->urlForRoute("assets/main.js") == webview::Url("http://127.0.0.1:5173/assets/main.js"));
    webview::WebApplicationOptions remoteOptions;
    remoteOptions.id = "demo-remote";
    remoteOptions.source = webview::RemoteOrigin { webview::Url("https://example.com") };
    assert(webview::createApplication(std::move(remoteOptions), &mappingError));
    webview::WebApplicationOptions invalidRemote;
    invalidRemote.id = "invalid-remote";
    invalidRemote.source = webview::RemoteOrigin { webview::Url("http://example.com") };
    assert(!webview::createApplication(std::move(invalidRemote), &mappingError));
}
