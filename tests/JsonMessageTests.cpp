#include "internal/Application.h"
#include "internal/BridgePageScript.h"
#include "internal/Diagnostics.h"
#include "internal/FileResource.h"
#include "internal/ResourceMapping.h"
#include "internal/RandomToken.h"
#include "internal/TextEncoding.h"
#include "webview/DocumentLifetime.h"
#include "webview/HostCompletion.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewState.h"
#include <system_webview/system_webview.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace
{
class TempDir final
{
public:
    TempDir()
    {
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt != 16; ++attempt) {
            path_ = root / ("system-webview-test-" + webview::randomToken128());
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("unable to create temporary directory");
    }

    ~TempDir()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    std::string path() const { return webview::pathToUtf8(path_); }
    std::string filePath(std::string_view name) const { return webview::pathToUtf8(path_ / webview::pathFromUtf8(name)); }
    bool isValid() const { return !path_.empty(); }

private:
    std::filesystem::path path_;
};

bool writeFile(const std::string& path, std::string_view bytes, bool truncate = true)
{
    std::ofstream file(webview::pathFromUtf8(path), std::ios::binary | (truncate ? std::ios::trunc : std::ios::app));
    return file && (file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())), file.good());
}

std::vector<std::string> capturedMessages;

void captureMessage(const webview::DiagnosticEvent& event)
{
    capturedMessages.push_back(event.category + "|" + event.message);
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
    EXPECT_TRUE(webview::BridgeMessage().payload.is_object());
    EXPECT_TRUE(webview::parseMessage("{}", &parsed, &error) && parsed.is_object() && parsed.empty());
    for (const std::string text :
         { "null", "[]", "true", "{", "{} trailing", "{\"x\":\"\xFF\"}", "{\"x\":\"\\uD800\"}", "{\"x\":1e999}" }) {
        EXPECT_TRUE(!webview::parseMessage(text, &parsed, &error));
        EXPECT_TRUE(!error.empty());
    }
    EXPECT_TRUE(!webview::parseMessage(std::string("{}\0trailing", 11), &parsed));
    EXPECT_TRUE(webview::parseMessage("{\"x\":1,\"x\":2}", &parsed));
    EXPECT_TRUE(parsed.at("x") == 2);
    const Json nulObject { { "text", std::string("a\0b", 3) } };
    const auto nulBytes = webview::serializeJson(nulObject);
    EXPECT_TRUE(nulBytes && nulBytes->find("\\u0000") != std::string::npos);
    EXPECT_TRUE(webview::parseMessage(*nulBytes, &parsed) && parsed == nulObject);
    EXPECT_TRUE(!webview::serializeJson(Json { { "x", std::string("\xFF") } }));
    EXPECT_TRUE(!webview::serializeJson(Json { { "x", std::numeric_limits<double>::quiet_NaN() } }));
    EXPECT_TRUE(!webview::serializeJson(Json { { "x", std::numeric_limits<double>::infinity() } }));
    EXPECT_TRUE(webview::serializeJson(Json("\"\\\n中文🌍")).value() == Json("\"\\\n中文🌍").dump());

    Json nested = Json::object();
    for (int depth = 0; depth < 120; ++depth) {
        nested = Json { { "child", std::move(nested) } };
    }
    const auto nestedBytes = webview::serializeJson(nested);
    EXPECT_TRUE(nestedBytes && webview::parseMessage(*nestedBytes, &parsed));
    for (int depth = 0; depth < 12; ++depth) {
        nested = Json { { "child", std::move(nested) } };
    }
    EXPECT_TRUE(!webview::serializeJson(nested));
    EXPECT_TRUE(!webview::parseMessage(nested.dump(), &parsed));

    webview::WebViewPolicyConfig config;
    config.pageToHostSchemas["number"] = { { { "value", webview::JsonType::Number } } };
    webview::WebViewPolicy policy(std::move(config));
    for (const Json value : { Json(-1), Json(std::uint64_t(18'446'744'073'709'551'615ULL)), Json(1.25) }) {
        EXPECT_TRUE(policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, "number", {}, Json { { "value", value } }, {} }));
    }
    EXPECT_TRUE(!policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, "number", {}, Json { { "value", nullptr } }, {} }));
    EXPECT_TRUE(!policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, "number", {}, nullptr, {} }));
    EXPECT_TRUE(webview::matchesJsonType(nullptr, webview::JsonType::Null));
    EXPECT_TRUE(webview::matchesJsonType(true, webview::JsonType::Boolean));
    EXPECT_TRUE(webview::matchesJsonType(Json::array(), webview::JsonType::Array));
    EXPECT_TRUE(webview::jsonString(Json::object(), "missing").empty());
    EXPECT_TRUE(webview::jsonString(Json { { "text", 1 } }, "text").empty());

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
    EXPECT_TRUE(received == 2);
    for (const Json version : { Json(1.5), Json("1"), Json(nullptr), Json(true) }) {
        envelope["version"] = version;
        bridge.receive(envelope.dump());
    }
    envelope["version"] = 1;
    for (const Json payload : { Json(nullptr), Json::array(), Json("text") }) {
        envelope["payload"] = payload;
        bridge.receive(envelope.dump());
    }
    EXPECT_TRUE(received == 2);
    bridge.emitEvent("test");
    EXPECT_TRUE(Json::parse(probe->sent.back()).at("payload").is_object());
    const auto sentCount = probe->sent.size();
    int failures = 0;
    for (const Json payload :
         { Json(nullptr), Json { { "x", std::string("\xFF") } }, Json { { "x", std::numeric_limits<double>::infinity() } } }) {
        bridge.call("test", payload, [&](const Json& result, const std::string& detail) {
            EXPECT_TRUE(result.is_object() && !detail.empty());
            ++failures;
        });
    }
    EXPECT_TRUE(failures == 3 && probe->sent.size() == sentCount);
    bridge.cancelPending();
    EXPECT_TRUE(failures == 3);
    bridge.setRequestHandler("test", [](const Json&, webview::WebViewBridge::Reply reply) {
        reply(Json { { "bad", std::numeric_limits<double>::quiet_NaN() } }, "");
        reply(Json::object(), "second reply");
    });
    envelope["kind"] = "request";
    envelope["requestId"] = "request-encoding-failure";
    envelope["payload"] = Json::object();
    bridge.receive(envelope.dump());
    EXPECT_TRUE(probe->sent.size() == sentCount + 1);
    const auto response = Json::parse(probe->sent.back());
    EXPECT_TRUE(response.at("kind") == "response" && response.at("payload").is_object());
    EXPECT_TRUE(!response.at("error").get<std::string>().empty());
}
}

TEST(CoreBehavior, ExistingCoverage)
{
    const auto tokenA = webview::randomToken128();
    const auto tokenB = webview::randomToken128();
    EXPECT_TRUE(tokenA.size() == 32);
    EXPECT_TRUE(tokenB.size() == 32);
    EXPECT_TRUE(tokenA != tokenB);
    EXPECT_TRUE(tokenA.find_first_not_of("0123456789abcdef") == std::string::npos);

    testJsonMigrationBoundaries();
    for (const std::string text : { "", "ASCII", "中文", "🌍", "\U00020000", "a中文🌍z" }) {
        const auto utf16 = webview::utf8ToUtf16(text);
        EXPECT_TRUE(utf16);
        const auto utf8 = webview::utf16ToUtf8(*utf16);
        EXPECT_TRUE(utf8 && *utf8 == text);
    }
    EXPECT_TRUE(webview::utf8ToUtf16("中文")->size() == 2);
    EXPECT_TRUE(webview::utf8ToUtf16("🌍")->size() == 2);
    EXPECT_TRUE(webview::utf16ToUtf8(u"🌍")->size() == 4);
    const std::string embeddedNul("a\0🌍", 6);
    const auto nulUtf16 = webview::utf8ToUtf16(embeddedNul);
    EXPECT_TRUE(nulUtf16 && nulUtf16->size() == 4 && (*nulUtf16)[1] == 0);
    EXPECT_TRUE(webview::utf16ToUtf8(*nulUtf16) == embeddedNul);
    for (const std::string text : { "\xFF", "\x80", "\xE4\xB8", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80" }) {
        EXPECT_TRUE(!webview::utf8ToUtf16(text));
        EXPECT_TRUE(!webview::utf8ToUtf16("valid prefix" + text));
    }
    for (const auto text : { std::u16string { 0xD800 },
                             std::u16string { 0xDC00 },
                             std::u16string { u'a', 0xD800, u'b' },
                             std::u16string { 0xDC00, 0xD800 } }) {
        EXPECT_TRUE(!webview::utf16ToUtf8(text));
    }
#ifdef _WIN32
    const auto wide = webview::utf8ToWide("中文🌍");
    EXPECT_TRUE(wide && wide->size() == 4);
    EXPECT_TRUE(webview::wideToUtf8(*wide) == "中文🌍");
    EXPECT_TRUE(webview::utf8ToWide("") == L"");
    EXPECT_TRUE(webview::wideToUtf8(L"") == "");
    EXPECT_TRUE(!webview::utf8ToWide(embeddedNul));
    EXPECT_TRUE(!webview::wideToUtf8(std::wstring(L"a\0b", 3)));
    EXPECT_TRUE(!webview::utf8ToWide("\xFF"));
    EXPECT_TRUE(!webview::wideToUtf8(std::wstring { static_cast<wchar_t>(0xD800) }));
#endif
    const webview::Json unicodeMessage { { "文本", "你好🌍" } };
    const auto jsonText = webview::serializeJson(unicodeMessage).value();
    const auto framedJson = "prefix" + jsonText + "suffix";
    webview::Json parsedMessage;
    std::string parseError;
    EXPECT_TRUE(webview::parseMessage(std::string_view(framedJson).substr(6, jsonText.size()), &parsedMessage, &parseError));
    EXPECT_TRUE(parsedMessage == unicodeMessage);
    EXPECT_TRUE(!webview::parseMessage("[]", &parsedMessage, &parseError));
    EXPECT_TRUE(!parseError.empty());

    const auto pageScript = webview::bridgePageScript("'%2-文档'", "native.send", "native.listen();", "const label = '%1';");
    EXPECT_TRUE(pageScript.find("const send = message => native.send({ documentToken: ('%2-文档'), message });") != std::string::npos);
    EXPECT_TRUE(pageScript.find("const label = '%1';") != std::string::npos);
    EXPECT_TRUE(pageScript.find("native.listen();") != std::string::npos);

    webview::setDiagnosticHandler(captureMessage);
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
        EXPECT_TRUE(runtimeFailureReceived);
        runtimeFailureReceived = false;
        diagnosticState.close();
        diagnosticState.emitRuntimeFailure({ webview::RuntimeFailureKind::WebContentProcessTerminated, "terminated", 9 });
        EXPECT_TRUE(!runtimeFailureReceived);
    }
    std::string diagnosticText;
    for (const auto& message : capturedMessages) {
        diagnosticText += message + "\n";
    }
    EXPECT_TRUE(diagnosticText.find("event=view.created") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("system_webview.lifecycle") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("event=view.ready") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("event=view.ready") == diagnosticText.rfind("event=view.ready"));
    EXPECT_TRUE(diagnosticText.find("event=navigation.failed") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("event=view.closed") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("session=41") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("origin=https://example.com") != std::string::npos);
    EXPECT_TRUE(diagnosticText.find("password") == std::string::npos);
    EXPECT_TRUE(diagnosticText.find("private") == std::string::npos);
    EXPECT_TRUE(diagnosticText.find("secret") == std::string::npos);
    webview::setDiagnosticHandler({});

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
    EXPECT_TRUE(eventReceived);
    eventReceived = false;
    bridge.receive((webview::Json { { "kind", "event" },
                                    { "type", "notify" },
                                    { "requestId", "" },
                                    { "payload", webview::Json { { "value", 7 } } },
                                    { "error", "" } })
                       .dump());
    EXPECT_TRUE(!eventReceived);
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
    EXPECT_TRUE(unicodeEvents == 1);
    bool callCompleted = false;
    bridge.call("查询🌍", { { "id", 9 } }, [&](const webview::Json& payload, const std::string& error) {
        callCompleted = error.empty() && payload.at("name").get<std::string>() == "item";
    });
    webview::Json request;
    request = webview::Json::parse(transportProbe->sent.back());
    EXPECT_TRUE(!request.empty());
    const auto requestId = request.at("requestId").get<std::string>();
    auto mismatchedResponse = request;
    mismatchedResponse["kind"] = "response";
    mismatchedResponse["type"] = "other";
    bridge.receive((mismatchedResponse).dump());
    EXPECT_TRUE(!callCompleted);
    bridge.receive((webview::Json { { "version", 1 },
                                    { "kind", "response" },
                                    { "type", "查询🌍" },
                                    { "requestId", requestId },
                                    { "payload", webview::Json { { "name", "item" } } },
                                    { "error", "" } })
                       .dump());
    EXPECT_TRUE(callCompleted);
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
    EXPECT_TRUE(!response.empty());
    EXPECT_TRUE(response.at("kind").get<std::string>() == "response");
    EXPECT_TRUE(response.at("payload").at("result").get<int>() == 5);
    bool cancelled = false;
    bridge.call("wait", webview::Json::object(), [&](const webview::Json&, const std::string& error) { cancelled = !error.empty(); });
    bridge.cancelPending();
    EXPECT_TRUE(cancelled);
    int invalidationCompletions = 0;
    bridge.call("close", webview::Json::object(), [&](const webview::Json&, const std::string&) {
        ++invalidationCompletions;
        bridge.call("reentrant", webview::Json::object(), [&](const webview::Json&, const std::string&) { ++invalidationCompletions; });
    });
    bridge.invalidate();
    EXPECT_TRUE(transportProbe->invalidated);
    EXPECT_TRUE(invalidationCompletions == 2);

    TempDir files;
    EXPECT_TRUE(files.isValid());
    const auto allowedFile = files.filePath("index.html");
    EXPECT_TRUE(writeFile(allowedFile, {}));
    const auto sourceFile = files.filePath("数据🌍.bin");
    EXPECT_TRUE(writeFile(sourceFile, "0123456789"));
    webview::WebResourceManager unboundResources(webview::Url("app://ui"));
    EXPECT_TRUE(unboundResources.publishFile(sourceFile).token.empty());
    auto resourceState = std::make_shared<webview::WebViewState>();
    auto& resources = *resourceState->resources;
    const webview::Url documentOrigin("https://trusted.example");
    resourceState->setResourceContext(webview::Url("app://ui"), documentOrigin, "文档-1");
    EXPECT_TRUE(resources.publishFile(sourceFile, {}, "other-doc").token.empty());
    const auto published = resources.publishFile(sourceFile);
    EXPECT_TRUE(!published.token.empty());
    EXPECT_TRUE(published.mimeType == "application/octet-stream");
    const auto htmlPublished = resources.publishFile(allowedFile);
    EXPECT_TRUE(!htmlPublished.token.empty());
    EXPECT_TRUE(htmlPublished.mimeType == "text/html");
    resources.release(htmlPublished.token);
    EXPECT_TRUE(published.url.toString().find(sourceFile) == std::string::npos);
    EXPECT_TRUE(writeFile(sourceFile, "abcdefghij"));
    auto rangeResponse = resources.open({ published.url, documentOrigin, "文档-1", "bytes=2-5" });
    EXPECT_TRUE(rangeResponse.status == 206 && rangeResponse.offset == 2 && rangeResponse.length == 4);
    std::array<char, 4> rangeBytes {};
    EXPECT_TRUE(rangeResponse.body->read(rangeBytes.data(), rangeBytes.size()) == 4);
    EXPECT_TRUE(std::string(rangeBytes.data(), rangeBytes.size()) == "2345");
    const auto snapshotPath = rangeResponse.body->path();
    EXPECT_TRUE(resources.open({ published.url, documentOrigin, "other-doc" }).status == 403);
    resources.release(published.token);
    EXPECT_TRUE(resources.open({ published.url, documentOrigin, "文档-1" }).status == 410);
    EXPECT_TRUE(std::filesystem::exists(webview::pathFromUtf8(snapshotPath)));
    rangeResponse.body.reset();
    rangeResponse.lease.reset();
    EXPECT_TRUE(!std::filesystem::exists(webview::pathFromUtf8(snapshotPath)));
    EXPECT_TRUE(std::filesystem::exists(webview::pathFromUtf8(sourceFile)));

    const auto emptyFile = files.filePath("empty.bin");
    EXPECT_TRUE(writeFile(emptyFile, {}));
    const auto emptyPublished = resources.publishFile(emptyFile);
    EXPECT_TRUE(!emptyPublished.token.empty());
    const auto emptyResponse = resources.open({ emptyPublished.url, documentOrigin, "文档-1" });
    EXPECT_TRUE(emptyResponse.status == 200 && emptyResponse.length == 0);
    EXPECT_TRUE(resources.open({ emptyPublished.url, documentOrigin, "文档-1", "bytes=0-0" }).status == 416);

    webview::WebViewPolicyConfig resourcePolicyConfig;
    resourcePolicyConfig.trustedHttpsOrigins.insert(documentOrigin.toString());
    auto resourceTransport = std::make_unique<MemoryBridgeTransport>();
    auto* resourceTransportProbe = resourceTransport.get();
    resourceState->policy = std::make_shared<webview::WebViewPolicy>(std::move(resourcePolicyConfig));
    resourceState->committedUrl = webview::Url("https://trusted.example/index.html");
    resourceState->bridge->setTransport(std::move(resourceTransport));
    resourceState->bindBridgePolicy();
    const auto controlResource = resources.publishFile(sourceFile);
    EXPECT_TRUE(!controlResource.token.empty());
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
    EXPECT_TRUE(resources.open({ controlResource.url, documentOrigin, "文档-1" }).status == 200);
    resourceState->bridge->receive(releaseMessage({ { "token", controlResource.token } }));
    EXPECT_TRUE(resources.open({ controlResource.url, documentOrigin, "文档-1" }).status == 410);
    EXPECT_TRUE(!resourceTransportProbe->sent.empty());
    const auto revokedEvent = webview::Json::parse(resourceTransportProbe->sent.back());
    EXPECT_TRUE(revokedEvent.at("type").get<std::string>() == "resource-revoked");
    EXPECT_TRUE(revokedEvent.at("payload").at("token").get<std::string>() == controlResource.token);

    webview::WebViewPolicyConfig config;
    config.allowedAppHosts.insert("ui");
    config.allowedFileRoots.push_back(files.path());
    config.trustedDevelopmentOrigins.insert("http://127.0.0.1:5173");
    config.trustedHttpsOrigins.insert("https://trusted.example");
    config.pageToHostSchemas["ping"] = { { { "sequence", webview::JsonType::Number } } };
    config.hostToPageSchemas["pong"] = { { { "accepted", webview::JsonType::Boolean } } };
    config.maximumBridgeMessageBytes = 128;
    const webview::WebViewPolicy policy(std::move(config));
    webview::WebViewPolicy copiedPolicy(policy);
    webview::WebViewPolicy assignedPolicy;
    assignedPolicy = policy;
    EXPECT_TRUE(copiedPolicy.allowsBridge(webview::Url("app://ui/page")));
    EXPECT_TRUE(assignedPolicy.maximumBridgeMessageBytes() == 128);

    const auto navigation = [&policy](std::string_view url) {
        return policy.decideNavigation({ webview::Url(url), true, true, false });
    };
    EXPECT_TRUE(navigation("https://example.com/page") == webview::NavigationDecision::Allow);
    EXPECT_TRUE(navigation("http://127.0.0.1:5173/") == webview::NavigationDecision::Allow);
    EXPECT_TRUE(navigation("http://127.0.0.1:5174/") == webview::NavigationDecision::Cancel);
    EXPECT_TRUE(navigation("app://ui/home") == webview::NavigationDecision::Allow);
    EXPECT_TRUE(navigation(webview::Url::fromLocalFile(allowedFile).toString()) == webview::NavigationDecision::Allow);
    EXPECT_TRUE(navigation("app://other/home") == webview::NavigationDecision::Cancel);
    EXPECT_TRUE(navigation("javascript:alert(1)") == webview::NavigationDecision::Cancel);
    EXPECT_TRUE(navigation("custom://host/path") == webview::NavigationDecision::Cancel);
    EXPECT_TRUE(navigation("not a url") == webview::NavigationDecision::Cancel);

    EXPECT_TRUE(policy.decideNewWindow({ webview::Url("https://example.com"), true }) == webview::NewWindowDecision::Cancel);
    EXPECT_TRUE(policy.decidePermission({ webview::PermissionKind::Camera, webview::Url("app://ui") }) == webview::PermissionDecision::Deny);
    EXPECT_TRUE(policy.decideDownload({ webview::Url("https://example.com/file"), {}, {} }) == webview::DownloadDecision::Cancel);
    EXPECT_TRUE(policy.allowsBridge(webview::Url("app://ui/page")));
    EXPECT_TRUE(policy.allowsBridge(webview::Url("http://127.0.0.1:5173/page")));
    EXPECT_TRUE(policy.allowsBridge(webview::Url("https://trusted.example/path")));
    EXPECT_TRUE(!policy.allowsBridge(webview::Url("https://trusted.example.evil/path")));

    std::string validationError;
    const webview::BridgeMessage validMessage { 1, webview::BridgeMessageKind::Event, "ping", {}, webview::Json { { "sequence", 1 } }, {} };
    EXPECT_TRUE(policy.validatePageToHostMessage(validMessage, &validationError));
    EXPECT_TRUE(validationError.empty());
    EXPECT_TRUE(!policy.validateHostToPageMessage(validMessage, &validationError));
    EXPECT_TRUE(validationError.find("type") != std::string::npos);
    EXPECT_TRUE(policy.validateHostToPageMessage(
        { 1, webview::BridgeMessageKind::Event, "pong", {}, { { "accepted", true } }, {} },
        &validationError));
    EXPECT_TRUE(!policy.validatePageToHostMessage(
        { 2, webview::BridgeMessageKind::Event, validMessage.type, {}, validMessage.payload, {} },
        &validationError));
    EXPECT_TRUE(validationError.find("version") != std::string::npos);
    EXPECT_TRUE(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, "unknown", {}, validMessage.payload, {} },
        &validationError));
    EXPECT_TRUE(validationError.find("type") != std::string::npos);
    EXPECT_TRUE(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json::object(), {} },
        &validationError));
    EXPECT_TRUE(validationError.find("sequence") != std::string::npos);
    EXPECT_TRUE(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json { { "sequence", "wrong type" } }, {} },
        &validationError));
    EXPECT_TRUE(validationError.find("number") != std::string::npos);
    EXPECT_TRUE(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json { { "sequence", 1 }, { "extra", true } } },
        &validationError));
    EXPECT_TRUE(validationError.find("unexpected") != std::string::npos);
    EXPECT_TRUE(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, validMessage.type, {}, webview::Json { { "sequence", std::string(200, 'x') } }, {} },
        &validationError));
    EXPECT_TRUE(validationError.find("size") != std::string::npos);

    webview::WebViewPolicyConfig unicodeConfig;
    unicodeConfig.pageToHostSchemas["查询🌍"] = { { { "名称", webview::JsonType::String } } };
    webview::WebViewPolicy unicodePolicy(std::move(unicodeConfig));
    webview::BridgeMessage unicodeEnvelope { 1, webview::BridgeMessageKind::Event, "查询🌍", {}, unicodeMessage, {} };
    EXPECT_TRUE(!unicodePolicy.validatePageToHostMessage(unicodeEnvelope, &validationError));
    EXPECT_TRUE(validationError == "Bridge message payload is missing required field: 名称");
    unicodeEnvelope.payload = { { "名称", "你好🌍" } };
    EXPECT_TRUE(unicodePolicy.validatePageToHostMessage(unicodeEnvelope, &validationError));
    EXPECT_TRUE(validationError.empty());

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
    EXPECT_TRUE(!oversizedMessageReceived);

    webview::DocumentLifetime lifetime;
    const auto firstDocument = lifetime.token();
    EXPECT_TRUE(lifetime.resultFor(firstDocument) == webview::DocumentError::None);
    lifetime.invalidate();
    EXPECT_TRUE(lifetime.resultFor(firstDocument) == webview::DocumentError::NavigationChanged);
    const auto secondDocument = lifetime.token();
    EXPECT_TRUE(lifetime.resultFor(secondDocument) == webview::DocumentError::None);
    lifetime.close();
    EXPECT_TRUE(lifetime.resultFor(secondDocument) == webview::DocumentError::Closed);
    lifetime.close();
    EXPECT_TRUE(lifetime.resultFor(lifetime.token()) == webview::DocumentError::Closed);

    auto state = std::make_shared<webview::WebViewState>();
    EXPECT_TRUE(state->initializationState() == webview::InitializationState::Initializing);
    bool queued = false;
    state->runWhenReady([&](const webview::InitializationResult& result) { queued = result.state == webview::InitializationState::Ready; });
    bool initialized = false;
    state->whenInitialized(
        [&](const webview::InitializationResult& result) { initialized = result.state == webview::InitializationState::Ready; });
    state->markReady();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(queued);
    EXPECT_TRUE(state->initializationState() == webview::InitializationState::Ready);

    auto failedState = std::make_shared<webview::WebViewState>();
    bool failed = false;
    failedState->whenInitialized([&](const webview::InitializationResult& result) {
        failed = result.state == webview::InitializationState::Failed && !result.error.empty();
    });
    failedState->failInitialization("test failure");
    EXPECT_TRUE(failed);

    bool queuedFailure = false;
    failedState->runWhenReady([&](const webview::InitializationResult& result) {
        queuedFailure = result.state == webview::InitializationState::Failed && result.error == "test failure";
    });
    EXPECT_TRUE(queuedFailure);

    auto closingState = std::make_shared<webview::WebViewState>();
    bool queuedClose = false;
    closingState->runWhenReady(
        [&](const webview::InitializationResult& result) { queuedClose = result.state == webview::InitializationState::Closed; });
    closingState->close();
    EXPECT_TRUE(queuedClose);

    webview::InitializationScheduler scheduler;
    std::vector<std::string> operationOrder;
    scheduler.runWhenReady([&](const webview::InitializationResult& result) {
        EXPECT_TRUE(result.state == webview::InitializationState::Ready);
        operationOrder.push_back("first");
        scheduler.runWhenReady([&](const webview::InitializationResult& nestedResult) {
            EXPECT_TRUE(nestedResult.state == webview::InitializationState::Ready);
            operationOrder.push_back("nested");
        });
    });
    scheduler.runWhenReady([&](const webview::InitializationResult&) { operationOrder.push_back("second"); });
    scheduler.markReady();
    EXPECT_TRUE(operationOrder == std::vector<std::string>({ "first", "second", "nested" }));

    auto completionState = std::make_shared<webview::WebViewState>();
    webview::HostCompletionGuard completionGuard(completionState);
    EXPECT_TRUE(completionGuard.claim().claim == webview::HostCompletionClaim::Accepted);
    EXPECT_TRUE(completionGuard.claim().claim == webview::HostCompletionClaim::Duplicate);

    webview::HostCompletionGuard closedGuard(completionState);
    completionState->close();
    EXPECT_TRUE(closedGuard.claim().claim == webview::HostCompletionClaim::OwnerUnavailable);
    webview::HostCompletionGuard destroyedGuard(completionState);
    completionState.reset();
    EXPECT_TRUE(destroyedGuard.claim().claim == webview::HostCompletionClaim::OwnerUnavailable);

    TempDir selectedRoot;
    EXPECT_TRUE(selectedRoot.isValid());
    const auto selectedFile = selectedRoot.filePath("selected.txt");
    EXPECT_TRUE(writeFile(selectedFile, {}));
    const auto indexFile = selectedRoot.filePath("index.html");
    EXPECT_TRUE(writeFile(indexFile, {}));
    const webview::FileSelectionRequest singleFileRequest { {}, {}, false, false };
    const auto selected = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile }, {} });
    EXPECT_TRUE(selected.status == webview::FileSelectionStatus::Selected);
    const auto empty = webview::normalizeFileSelectionResult(singleFileRequest, { webview::FileSelectionStatus::Selected, {}, {} });
    EXPECT_TRUE(empty.status == webview::FileSelectionStatus::Cancelled);
    const auto multiple = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile, selectedFile }, {} });
    EXPECT_TRUE(multiple.status == webview::FileSelectionStatus::InvalidResult);
    const auto directory = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedRoot.path() }, {} });
    EXPECT_TRUE(directory.status == webview::FileSelectionStatus::InvalidResult);

    std::vector<webview::ResourceMapping> mappings {
        { webview::Url("app://demo"), selectedRoot.path(), "index.html", true }
    };
    std::string mappingError;
    EXPECT_TRUE(webview::validateResourceMappings(&mappings, &mappingError));
    EXPECT_TRUE(
        mappings.front().localDirectory == webview::pathToUtf8(std::filesystem::canonical(webview::pathFromUtf8(selectedRoot.path()))));
    const auto* mapping = webview::findResourceMapping(mappings, webview::Url("app://demo/selected.txt"));
    EXPECT_TRUE(mapping);
    EXPECT_TRUE(
        webview::resolveMappedResource(*mapping, webview::Url("app://demo/selected.txt"), &mappingError)
        == webview::pathToUtf8(std::filesystem::canonical(webview::pathFromUtf8(selectedFile))));
    EXPECT_TRUE(webview::resolveMappedResource(*mapping, webview::Url("app://demo/%2e%2e/secret"), &mappingError).empty());
    EXPECT_TRUE(webview::resolveMappedResource(*mapping, webview::Url("app://demo/missing.txt"), &mappingError).empty());
    EXPECT_TRUE(
        webview::resolveMappedResource(*mapping, webview::Url("app://demo/orders/42"), &mappingError, true)
        == webview::pathToUtf8(std::filesystem::canonical(webview::pathFromUtf8(indexFile))));
    EXPECT_TRUE(webview::resolveMappedResource(*mapping, webview::Url("app://demo/missing.js"), &mappingError, true).empty());
    const auto deniedNetworkPolicy = webview::localBundleContentSecurityPolicy(webview::ExternalNetworkAccess::Denied);
    EXPECT_TRUE(deniedNetworkPolicy.find("default-src 'self'") != std::string::npos);
    EXPECT_TRUE(deniedNetworkPolicy.find("connect-src 'self'") != std::string::npos);
    EXPECT_TRUE(deniedNetworkPolicy.find("form-action 'self'") != std::string::npos);
    EXPECT_TRUE(webview::localBundleContentSecurityPolicy(webview::ExternalNetworkAccess::Allowed).empty());
    TempDir outsideRoot;
    EXPECT_TRUE(outsideRoot.isValid());
    const auto outsideFile = outsideRoot.filePath("secret.txt");
    EXPECT_TRUE(writeFile(outsideFile, {}));
    const auto linkedFile = selectedRoot.filePath("linked.txt");
    std::error_code linkError;
    std::filesystem::create_symlink(outsideFile, linkedFile, linkError);
    if (!linkError && std::filesystem::is_symlink(linkedFile)) {
        EXPECT_TRUE(webview::resolveMappedResource(*mapping, webview::Url("app://demo/linked.txt"), &mappingError).empty());
    }

    std::vector<webview::ResourceMapping> duplicateMappings { { webview::Url("app://demo"), selectedRoot.path() },
                                                              { webview::Url("APP://DEMO"), selectedRoot.path() } };
    EXPECT_TRUE(webview::validateResourceMappings(&duplicateMappings, &mappingError));
    std::vector<webview::ResourceMapping> invalidMappings { { webview::Url("https://demo/path"), selectedRoot.path() } };
    EXPECT_TRUE(!webview::validateResourceMappings(&invalidMappings, &mappingError));

    const auto createBundleWithId = [&selectedRoot](const std::string& id) {
        webview::WebApplicationOptions options;
        options.id = id;
        options.source = webview::LocalBundle { selectedRoot.path() };
        return webview::createApplication(std::move(options), nullptr);
    };
    for (const std::string id : { "demo-42", "中文应用", "é", "١٢", "Ⅻ", "²", "\U00020000", "\U00010400", "---" }) {
        const auto application = createBundleWithId(id);
        EXPECT_TRUE(application && application->id() == id);
        const auto origin = application->origin();
        EXPECT_TRUE(origin.isValid() && !origin.host().empty());
        const auto route = application->urlForRoute("index.html");
        std::vector<webview::ResourceMapping> unicodeMappings { { origin, selectedRoot.path() } };
        EXPECT_TRUE(webview::validateResourceMappings(&unicodeMappings, &mappingError));
        const auto* unicodeMapping = webview::findResourceMapping(unicodeMappings, route);
        EXPECT_TRUE(unicodeMapping);
        EXPECT_TRUE(
            webview::resolveMappedResource(*unicodeMapping, route, &mappingError)
            == webview::pathToUtf8(std::filesystem::canonical(webview::pathFromUtf8(indexFile))));
        webview::WebViewPolicyConfig unicodeConfig;
        unicodeConfig.allowedAppHosts.insert(origin.host());
        const webview::WebViewPolicy unicodePolicy(std::move(unicodeConfig));
        EXPECT_TRUE(unicodePolicy.decideNavigation({ route }) == webview::NavigationDecision::Allow);
        EXPECT_TRUE(unicodePolicy.allowsBridge(route));
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
        EXPECT_TRUE(!createBundleWithId(id));
    }

    webview::WebApplicationOptions bundleOptions;
    bundleOptions.id = "demo-app";
    bundleOptions.source = webview::LocalBundle { selectedRoot.path() };
    const auto bundleApplication = webview::createApplication(std::move(bundleOptions), &mappingError);
    EXPECT_TRUE(bundleApplication);
    EXPECT_TRUE(std::get<webview::LocalBundle>(bundleApplication->source()).externalNetworkAccess == webview::ExternalNetworkAccess::Denied);
    EXPECT_TRUE(bundleApplication->urlForRoute("orders/42") == webview::Url("app://demo-app/orders/42"));
    EXPECT_TRUE(bundleApplication->urlForRoute("订单/详情") == webview::Url("app://demo-app/订单/详情"));
    const auto unicodeFile = selectedRoot.filePath("数据.txt");
    EXPECT_TRUE(writeFile(unicodeFile, {}));
    EXPECT_TRUE(
        webview::resolveMappedResource(*mapping, webview::Url("app://demo/数据.txt"), &mappingError)
        == webview::pathToUtf8(std::filesystem::canonical(webview::pathFromUtf8(unicodeFile))));
    webview::WebApplicationOptions invalidNetworkAccess;
    invalidNetworkAccess.id = "invalid-network-access";
    invalidNetworkAccess.source =
        webview::LocalBundle { selectedRoot.path(), "index.html", true, static_cast<webview::ExternalNetworkAccess>(-1) };
    EXPECT_TRUE(!webview::createApplication(std::move(invalidNetworkAccess), &mappingError));
    webview::WebApplicationOptions devOptions;
    devOptions.id = "demo-dev";
    devOptions.source = webview::DevelopmentServer { webview::Url("http://127.0.0.1:5173") };
    const auto devApplication = webview::createApplication(std::move(devOptions), &mappingError);
    EXPECT_TRUE(devApplication && devApplication->urlForRoute("assets/main.js") == webview::Url("http://127.0.0.1:5173/assets/main.js"));
    webview::WebApplicationOptions remoteOptions;
    remoteOptions.id = "demo-remote";
    remoteOptions.source = webview::RemoteOrigin { webview::Url("https://example.com") };
    EXPECT_TRUE(webview::createApplication(std::move(remoteOptions), &mappingError));
    webview::WebApplicationOptions invalidRemote;
    invalidRemote.id = "invalid-remote";
    invalidRemote.source = webview::RemoteOrigin { webview::Url("http://example.com") };
    EXPECT_TRUE(!webview::createApplication(std::move(invalidRemote), &mappingError));
}
