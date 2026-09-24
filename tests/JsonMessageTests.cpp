#include "internal/Application.h"
#include "internal/ResourceMapping.h"
#include "webview/DocumentLifetime.h"
#include "webview/HostCompletion.h"
#include "webview/WebResourceManager.h"
#include "webview/WebViewBridge.h"
#include "webview/WebViewPolicy.h"
#include "webview/WebViewState.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cassert>

namespace
{
class MemoryBridgeTransport final : public webview::BridgeTransport
{
public:
    bool send(const QByteArray& message) override
    {
        sent.push_back(message);
        return true;
    }

    void invalidate() override { invalidated = true; }

    QVector<QByteArray> sent;
    bool invalidated = false;
};
}

int main()
{
    auto transport = std::make_unique<MemoryBridgeTransport>();
    auto* transportProbe = transport.get();
    webview::WebViewBridge bridge(std::move(transport));
    bool eventReceived = false;
    bridge.on(QStringLiteral("notify"), [&](const QJsonObject& payload) {
        eventReceived = payload.value(QStringLiteral("value")).toInt() == 7;
    });
    bridge.receive(QJsonDocument(
                       QJsonObject { { "version", 1 },
                                     { "kind", "event" },
                                     { "type", "notify" },
                                     { "requestId", "" },
                                     { "payload", QJsonObject { { "value", 7 } } },
                                     { "error", "" } })
                       .toJson(QJsonDocument::Compact));
    assert(eventReceived);
    eventReceived = false;
    bridge.receive(QJsonDocument(
                       QJsonObject { { "kind", "event" },
                                     { "type", "notify" },
                                     { "requestId", "" },
                                     { "payload", QJsonObject { { "value", 7 } } },
                                     { "error", "" } })
                       .toJson(QJsonDocument::Compact));
    assert(!eventReceived);
    bool callCompleted = false;
    bridge.call(QStringLiteral("lookup"), { { "id", 9 } }, [&](const QJsonObject& payload, const QString& error) {
        callCompleted = error.isEmpty() && payload.value("name").toString() == QStringLiteral("item");
    });
    QJsonObject request;
    request = QJsonDocument::fromJson(transportProbe->sent.back()).object();
    assert(!request.isEmpty());
    const auto requestId = request.value(QStringLiteral("requestId")).toString();
    bridge.receive(QJsonDocument(
                       QJsonObject { { "version", 1 },
                                     { "kind", "response" },
                                     { "type", "lookup" },
                                     { "requestId", requestId },
                                     { "payload", QJsonObject { { "name", "item" } } },
                                     { "error", "" } })
                       .toJson(QJsonDocument::Compact));
    assert(callCompleted);
    bridge.onRequest(QStringLiteral("sum"), [](const QJsonObject& payload, webview::WebViewBridge::Reply reply) {
        reply({ { "result", payload.value("left").toInt() + payload.value("right").toInt() } }, { });
    });
    bridge.receive(QJsonDocument(
                       QJsonObject { { "version", 1 },
                                     { "kind", "request" },
                                     { "type", "sum" },
                                     { "requestId", "request-1" },
                                     { "payload", QJsonObject { { "left", 2 }, { "right", 3 } } },
                                     { "error", "" } })
                       .toJson(QJsonDocument::Compact));
    QJsonObject response;
    response = QJsonDocument::fromJson(transportProbe->sent.back()).object();
    assert(!response.isEmpty());
    assert(response.value("kind").toString() == QStringLiteral("response"));
    assert(response.value("payload").toObject().value("result").toInt() == 5);
    bool cancelled = false;
    bridge.call(QStringLiteral("wait"), { }, [&](const QJsonObject&, const QString& error) { cancelled = !error.isEmpty(); });
    bridge.cancelPending();
    assert(cancelled);
    int invalidationCompletions = 0;
    bridge.call(QStringLiteral("close"), { }, [&](const QJsonObject&, const QString&) {
        ++invalidationCompletions;
        bridge.call(QStringLiteral("reentrant"), { }, [&](const QJsonObject&, const QString&) { ++invalidationCompletions; });
    });
    bridge.invalidate();
    assert(transportProbe->invalidated);
    assert(invalidationCompletions == 2);

    QTemporaryDir files;
    assert(files.isValid());
    QFile allowedFile(files.filePath(QStringLiteral("index.html")));
    assert(allowedFile.open(QIODevice::WriteOnly));
    allowedFile.close();

    QFile sourceFile(files.filePath(QStringLiteral("large.bin")));
    assert(sourceFile.open(QIODevice::WriteOnly));
    assert(sourceFile.write("0123456789") == 10);
    sourceFile.close();
    webview::WebResourceManager unboundResources(QUrl(QStringLiteral("app://ui")));
    assert(unboundResources.publishFile(sourceFile.fileName()).token.isEmpty());
    auto resourceState = std::make_shared<webview::WebViewState>();
    auto& resources = *resourceState->resources;
    const QUrl documentOrigin(QStringLiteral("https://trusted.example"));
    resourceState->setResourceContext(QUrl(QStringLiteral("app://ui")), documentOrigin, QStringLiteral("doc-1"));
    assert(resources.publishFile(sourceFile.fileName(), { }, QStringLiteral("other-doc")).token.isEmpty());
    const auto published = resources.publishFile(sourceFile.fileName());
    assert(!published.token.isEmpty());
    assert(!published.url.toString().contains(sourceFile.fileName()));
    assert(sourceFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(sourceFile.write("abcdefghij") == 10);
    sourceFile.close();
    auto rangeResponse = resources.open({ published.url, documentOrigin, QStringLiteral("doc-1"), QStringLiteral("bytes=2-5") });
    assert(rangeResponse.status == 206 && rangeResponse.offset == 2 && rangeResponse.length == 4);
    assert(rangeResponse.body->readAll() == QByteArray("2345"));
    const auto snapshotPath = qobject_cast<QFile*>(rangeResponse.body.get())->fileName();
    assert(resources.open({ published.url, documentOrigin, QStringLiteral("other-doc") }).status == 403);
    resources.release(published.token);
    assert(resources.open({ published.url, documentOrigin, QStringLiteral("doc-1") }).status == 410);
    assert(QFileInfo::exists(snapshotPath));
    rangeResponse.body.reset();
    rangeResponse.lease.reset();
    assert(!QFileInfo::exists(snapshotPath));
    assert(QFileInfo::exists(sourceFile.fileName()));

    QFile emptyFile(files.filePath(QStringLiteral("empty.bin")));
    assert(emptyFile.open(QIODevice::WriteOnly));
    emptyFile.close();
    const auto emptyPublished = resources.publishFile(emptyFile.fileName());
    assert(!emptyPublished.token.isEmpty());
    const auto emptyResponse = resources.open({ emptyPublished.url, documentOrigin, QStringLiteral("doc-1") });
    assert(emptyResponse.status == 200 && emptyResponse.length == 0);
    assert(resources.open({ emptyPublished.url, documentOrigin, QStringLiteral("doc-1"), QStringLiteral("bytes=0-0") }).status == 416);

    webview::WebViewPolicyConfig resourcePolicyConfig;
    resourcePolicyConfig.trustedHttpsOrigins.insert(documentOrigin.toString());
    auto resourceTransport = std::make_unique<MemoryBridgeTransport>();
    auto* resourceTransportProbe = resourceTransport.get();
    resourceState->policy = std::make_shared<webview::WebViewPolicy>(std::move(resourcePolicyConfig));
    resourceState->committedUrl = QUrl(QStringLiteral("https://trusted.example/index.html"));
    resourceState->bridge->setTransport(std::move(resourceTransport));
    resourceState->bindBridgePolicy();
    const auto controlResource = resources.publishFile(sourceFile.fileName());
    assert(!controlResource.token.isEmpty());
    const auto releaseMessage = [&controlResource](const QJsonObject& payload) {
        return QJsonDocument(
                   QJsonObject { { "version", 1 },
                                 { "kind", "event" },
                                 { "type", "release-resource" },
                                 { "requestId", "" },
                                 { "payload", payload },
                                 { "error", "" } })
            .toJson(QJsonDocument::Compact);
    };
    resourceState->bridge->receive(releaseMessage({ { "token", controlResource.token }, { "extra", true } }));
    assert(resources.open({ controlResource.url, documentOrigin, QStringLiteral("doc-1") }).status == 200);
    resourceState->bridge->receive(releaseMessage({ { "token", controlResource.token } }));
    assert(resources.open({ controlResource.url, documentOrigin, QStringLiteral("doc-1") }).status == 410);
    assert(!resourceTransportProbe->sent.isEmpty());
    const auto revokedEvent = QJsonDocument::fromJson(resourceTransportProbe->sent.back()).object();
    assert(revokedEvent.value(QStringLiteral("type")).toString() == QStringLiteral("resource-revoked"));
    assert(revokedEvent.value(QStringLiteral("payload")).toObject().value(QStringLiteral("token")).toString() == controlResource.token);

    webview::WebViewPolicyConfig config;
    config.allowedAppHosts.insert(QStringLiteral("ui"));
    config.allowedFileRoots.append(files.path());
    config.trustedDevelopmentOrigins.insert(QStringLiteral("http://127.0.0.1:5173"));
    config.trustedHttpsOrigins.insert(QStringLiteral("https://trusted.example"));
    config.pageToHostSchemas.insert(QStringLiteral("ping"), { { { QStringLiteral("sequence"), QJsonValue::Double } } });
    config.hostToPageSchemas.insert(QStringLiteral("pong"), { { { QStringLiteral("accepted"), QJsonValue::Bool } } });
    config.maximumBridgeMessageBytes = 128;
    const webview::WebViewPolicy policy(std::move(config));

    const auto navigation = [&policy](const QString& url) {
        return policy.decideNavigation({ QUrl(url), true, true, false });
    };
    assert(navigation(QStringLiteral("https://example.com/page")) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("http://127.0.0.1:5173/")) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("http://127.0.0.1:5174/")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("app://ui/home")) == webview::NavigationDecision::Allow);
    assert(navigation(QUrl::fromLocalFile(allowedFile.fileName()).toString()) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("app://other/home")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("javascript:alert(1)")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("custom://host/path")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("not a url")) == webview::NavigationDecision::Cancel);

    assert(policy.decideNewWindow({ QUrl(QStringLiteral("https://example.com")), true }) == webview::NewWindowDecision::Cancel);
    assert(
        policy.decidePermission({ webview::PermissionKind::Camera, QUrl(QStringLiteral("app://ui")) })
        == webview::PermissionDecision::Deny);
    assert(policy.decideDownload({ QUrl(QStringLiteral("https://example.com/file")), { }, { } }) == webview::DownloadDecision::Cancel);
    assert(policy.allowsBridge(QUrl(QStringLiteral("app://ui/page"))));
    assert(policy.allowsBridge(QUrl(QStringLiteral("http://127.0.0.1:5173/page"))));
    assert(policy.allowsBridge(QUrl(QStringLiteral("https://trusted.example/path"))));
    assert(!policy.allowsBridge(QUrl(QStringLiteral("https://trusted.example.evil/path"))));

    QString validationError;
    const webview::BridgeMessage validMessage {
        1, webview::BridgeMessageKind::Event, QStringLiteral("ping"), { }, QJsonObject { { QStringLiteral("sequence"), 1 } }, { }
    };
    assert(policy.validatePageToHostMessage(validMessage, &validationError));
    assert(validationError.isEmpty());
    assert(!policy.validateHostToPageMessage(validMessage, &validationError));
    assert(validationError.contains(QStringLiteral("type")));
    assert(policy.validateHostToPageMessage(
        { 1, webview::BridgeMessageKind::Event, QStringLiteral("pong"), { }, { { QStringLiteral("accepted"), true } }, { } },
        &validationError));
    assert(!policy.validatePageToHostMessage(
        { 2, webview::BridgeMessageKind::Event, validMessage.type, { }, validMessage.payload, { } },
        &validationError));
    assert(validationError.contains(QStringLiteral("version")));
    assert(!policy.validatePageToHostMessage(
        { 1, webview::BridgeMessageKind::Event, QStringLiteral("unknown"), { }, validMessage.payload, { } },
        &validationError));
    assert(validationError.contains(QStringLiteral("type")));
    assert(!policy.validatePageToHostMessage({ 1, webview::BridgeMessageKind::Event, validMessage.type, { }, { }, { } }, &validationError));
    assert(validationError.contains(QStringLiteral("sequence")));
    assert(!policy.validatePageToHostMessage(
        { 1,
          webview::BridgeMessageKind::Event,
          validMessage.type,
          { },
          QJsonObject { { QStringLiteral("sequence"), QStringLiteral("wrong type") } },
          { } },
        &validationError));
    assert(validationError.contains(QStringLiteral("number")));
    assert(!policy.validatePageToHostMessage(
        { 1,
          webview::BridgeMessageKind::Event,
          validMessage.type,
          { },
          QJsonObject { { QStringLiteral("sequence"), 1 }, { QStringLiteral("extra"), true } } },
        &validationError));
    assert(validationError.contains(QStringLiteral("unexpected")));
    assert(!policy.validatePageToHostMessage(
        { 1,
          webview::BridgeMessageKind::Event,
          validMessage.type,
          { },
          QJsonObject { { QStringLiteral("sequence"), QString(200, QLatin1Char('x')) } },
          { } },
        &validationError));
    assert(validationError.contains(QStringLiteral("size")));

    webview::WebViewPolicyConfig wireLimitConfig;
    wireLimitConfig.allowedAppHosts.insert(QStringLiteral("ui"));
    wireLimitConfig.pageToHostSchemas.insert(QStringLiteral("notify"), { });
    wireLimitConfig.maximumBridgeMessageBytes = 128;
    auto wireLimitState = std::make_shared<webview::WebViewState>();
    wireLimitState->policy = std::make_shared<webview::WebViewPolicy>(std::move(wireLimitConfig));
    wireLimitState->committedUrl = QUrl(QStringLiteral("app://ui/index.html"));
    wireLimitState->bindBridgePolicy();
    bool oversizedMessageReceived = false;
    wireLimitState->bridge->on(QStringLiteral("notify"), [&](const QJsonObject&) { oversizedMessageReceived = true; });
    const QByteArray compactMessage = QJsonDocument(
                                          QJsonObject { { "version", 1 },
                                                        { "kind", "event" },
                                                        { "type", "notify" },
                                                        { "requestId", "" },
                                                        { "payload", QJsonObject() },
                                                        { "error", "" } })
                                          .toJson(QJsonDocument::Compact);
    wireLimitState->bridge->receive(compactMessage + QByteArray(128, ' '));
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
        failed = result.state == webview::InitializationState::Failed && !result.error.isEmpty();
    });
    failedState->failInitialization(QStringLiteral("test failure"));
    assert(failed);

    bool queuedFailure = false;
    failedState->runWhenReady([&](const webview::InitializationResult& result) {
        queuedFailure = result.state == webview::InitializationState::Failed && result.error == QStringLiteral("test failure");
    });
    assert(queuedFailure);

    auto closingState = std::make_shared<webview::WebViewState>();
    bool queuedClose = false;
    closingState->runWhenReady(
        [&](const webview::InitializationResult& result) { queuedClose = result.state == webview::InitializationState::Closed; });
    closingState->close();
    assert(queuedClose);

    webview::InitializationScheduler scheduler;
    QStringList operationOrder;
    scheduler.runWhenReady([&](const webview::InitializationResult& result) {
        assert(result.state == webview::InitializationState::Ready);
        operationOrder.push_back(QStringLiteral("first"));
        scheduler.runWhenReady([&](const webview::InitializationResult& nestedResult) {
            assert(nestedResult.state == webview::InitializationState::Ready);
            operationOrder.push_back(QStringLiteral("nested"));
        });
    });
    scheduler.runWhenReady([&](const webview::InitializationResult&) { operationOrder.push_back(QStringLiteral("second")); });
    scheduler.markReady();
    assert(operationOrder == QStringList({ QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("nested") }));

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
    const webview::FileSelectionRequest singleFileRequest { { }, { }, false, false };
    const auto selected =
        webview::normalizeFileSelectionResult(singleFileRequest, { webview::FileSelectionStatus::Selected, { selectedFile }, { } });
    assert(selected.status == webview::FileSelectionStatus::Selected);
    const auto empty = webview::normalizeFileSelectionResult(singleFileRequest, { webview::FileSelectionStatus::Selected, { }, { } });
    assert(empty.status == webview::FileSelectionStatus::Cancelled);
    const auto multiple = webview::normalizeFileSelectionResult(
        singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile, selectedFile }, { } });
    assert(multiple.status == webview::FileSelectionStatus::InvalidResult);
    const auto directory =
        webview::normalizeFileSelectionResult(singleFileRequest, { webview::FileSelectionStatus::Selected, { selectedRoot.path() }, { } });
    assert(directory.status == webview::FileSelectionStatus::InvalidResult);

    QVector<webview::ResourceMapping> mappings {
        { QUrl(QStringLiteral("app://demo")), selectedRoot.path(), QStringLiteral("index.html"), true }
    };
    QString mappingError;
    assert(webview::validateResourceMappings(&mappings, &mappingError));
    assert(mappings.front().localDirectory == QFileInfo(selectedRoot.path()).canonicalFilePath());
    const auto* mapping = webview::findResourceMapping(mappings, QUrl(QStringLiteral("app://demo/selected.txt")));
    assert(mapping);
    assert(
        webview::resolveMappedResource(*mapping, QUrl(QStringLiteral("app://demo/selected.txt")), &mappingError)
        == QFileInfo(selectedFile).canonicalFilePath());
    assert(webview::resolveMappedResource(*mapping, QUrl(QStringLiteral("app://demo/%2e%2e/secret")), &mappingError).isEmpty());
    assert(webview::resolveMappedResource(*mapping, QUrl(QStringLiteral("app://demo/missing.txt")), &mappingError).isEmpty());
    assert(
        webview::resolveMappedResource(*mapping, QUrl(QStringLiteral("app://demo/orders/42")), &mappingError, true)
        == QFileInfo(indexFile.fileName()).canonicalFilePath());
    assert(webview::resolveMappedResource(*mapping, QUrl(QStringLiteral("app://demo/missing.js")), &mappingError, true).isEmpty());
    QTemporaryDir outsideRoot;
    assert(outsideRoot.isValid());
    const auto outsideFile = outsideRoot.filePath(QStringLiteral("secret.txt"));
    QFile secret(outsideFile);
    assert(secret.open(QIODevice::WriteOnly));
    secret.close();
    const auto linkedFile = selectedRoot.filePath(QStringLiteral("linked.txt"));
    assert(QFile::link(outsideFile, linkedFile));
    if (QFileInfo(linkedFile).isSymLink()) {
        assert(webview::resolveMappedResource(*mapping, QUrl(QStringLiteral("app://demo/linked.txt")), &mappingError).isEmpty());
    }

    QVector<webview::ResourceMapping> duplicateMappings { { QUrl(QStringLiteral("app://demo")), selectedRoot.path() },
                                                          { QUrl(QStringLiteral("APP://DEMO")), selectedRoot.path() } };
    assert(!webview::validateResourceMappings(&duplicateMappings, &mappingError));
    QVector<webview::ResourceMapping> invalidMappings { { QUrl(QStringLiteral("https://demo/path")), selectedRoot.path() } };
    assert(!webview::validateResourceMappings(&invalidMappings, &mappingError));

    webview::WebApplicationOptions bundleOptions;
    bundleOptions.id = QStringLiteral("demo-app");
    bundleOptions.source = webview::LocalBundle { selectedRoot.path() };
    const auto bundleApplication = webview::createApplication(std::move(bundleOptions), &mappingError);
    assert(bundleApplication);
    assert(bundleApplication->urlForRoute(QStringLiteral("orders/42")) == QUrl(QStringLiteral("app://demo-app/orders/42")));
    webview::WebApplicationOptions devOptions;
    devOptions.id = QStringLiteral("demo-dev");
    devOptions.source = webview::DevelopmentServer { QUrl(QStringLiteral("http://127.0.0.1:5173")) };
    const auto devApplication = webview::createApplication(std::move(devOptions), &mappingError);
    assert(
        devApplication
        && devApplication->urlForRoute(QStringLiteral("assets/main.js")) == QUrl(QStringLiteral("http://127.0.0.1:5173/assets/main.js")));
    webview::WebApplicationOptions remoteOptions;
    remoteOptions.id = QStringLiteral("demo-remote");
    remoteOptions.source = webview::RemoteOrigin { QUrl(QStringLiteral("https://example.com")) };
    assert(webview::createApplication(std::move(remoteOptions), &mappingError));
    webview::WebApplicationOptions invalidRemote;
    invalidRemote.id = QStringLiteral("invalid-remote");
    invalidRemote.source = webview::RemoteOrigin { QUrl(QStringLiteral("http://example.com")) };
    assert(!webview::createApplication(std::move(invalidRemote), &mappingError));
}
