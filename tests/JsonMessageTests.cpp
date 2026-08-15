#include "webview/DocumentLifetime.h"
#include "webview/HostCompletion.h"
#include "webview/ResourceMapping.h"
#include "webview/JsonMessage.h"
#include "webview/WebViewPolicy.h"
#include "webview/WebViewState.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cassert>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

int main()
{
    QJsonObject input { { "type", "ping" }, { "sequence", 7 } };
    QJsonObject output;
    assert(webview::parseMessage(webview::jsonForJavaScriptArgument(input), &output));
    assert(output == input);
    assert(!webview::parseMessage(QStringLiteral("[]"), &output));

    QTemporaryDir files;
    assert(files.isValid());
    QFile allowedFile(files.filePath(QStringLiteral("index.html")));
    assert(allowedFile.open(QIODevice::WriteOnly));
    allowedFile.close();

    webview::WebViewPolicyConfig config;
    config.allowedAppHosts.insert(QStringLiteral("ui"));
    config.allowedFileRoots.append(files.path());
    config.trustedHttpsOrigins.insert(QStringLiteral("https://trusted.example"));
    config.bridgeSchemas.insert(QStringLiteral("ping"), { QSet<QString> { QStringLiteral("sequence") } });
    config.maximumBridgeMessageBytes = 128;
    const webview::WebViewPolicy policy(std::move(config));

    const auto navigation = [&policy](const QString& url) {
        return policy.decideNavigation({ QUrl(url), true, true, false });
    };
    assert(navigation(QStringLiteral("https://example.com/page")) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("app://ui/home")) == webview::NavigationDecision::Allow);
    assert(navigation(QUrl::fromLocalFile(allowedFile.fileName()).toString()) == webview::NavigationDecision::Allow);
    assert(navigation(QStringLiteral("app://other/home")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("javascript:alert(1)")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("custom://host/path")) == webview::NavigationDecision::Cancel);
    assert(navigation(QStringLiteral("not a url")) == webview::NavigationDecision::Cancel);

    assert(policy.decideNewWindow({ QUrl(QStringLiteral("https://example.com")), true })
        == webview::NewWindowDecision::Cancel);
    assert(policy.decidePermission({ webview::PermissionKind::Camera, QUrl(QStringLiteral("app://ui")) })
        == webview::PermissionDecision::Deny);
    assert(policy.decideDownload({ QUrl(QStringLiteral("https://example.com/file")), { }, { } })
        == webview::DownloadDecision::Cancel);
    assert(policy.allowsBridge(QUrl(QStringLiteral("app://ui/page"))));
    assert(policy.allowsBridge(QUrl(QStringLiteral("https://trusted.example/path"))));
    assert(!policy.allowsBridge(QUrl(QStringLiteral("https://trusted.example.evil/path"))));

    QString validationError;
    const webview::BridgeMessage validMessage {
        1, QStringLiteral("ping"), QJsonObject { { QStringLiteral("sequence"), 1 } }
    };
    assert(policy.validateBridgeMessage(validMessage, &validationError));
    assert(validationError.isEmpty());
    assert(!policy.validateBridgeMessage({ 2, validMessage.type, validMessage.payload }, &validationError));
    assert(validationError.contains(QStringLiteral("version")));
    assert(!policy.validateBridgeMessage({ 1, QStringLiteral("unknown"), validMessage.payload }, &validationError));
    assert(validationError.contains(QStringLiteral("type")));
    assert(!policy.validateBridgeMessage({ 1, validMessage.type, { } }, &validationError));
    assert(validationError.contains(QStringLiteral("sequence")));
    assert(!policy.validateBridgeMessage(
        { 1, validMessage.type, QJsonObject { { QStringLiteral("sequence"), QString(200, QLatin1Char('x')) } } },
        &validationError));
    assert(validationError.contains(QStringLiteral("size")));

    webview::DocumentLifetime lifetime;
    const auto firstDocument = lifetime.token();
    assert(lifetime.resultFor(firstDocument) == webview::MessageError::None);
    lifetime.invalidate();
    assert(lifetime.resultFor(firstDocument) == webview::MessageError::NavigationChanged);
    const auto secondDocument = lifetime.token();
    assert(lifetime.resultFor(secondDocument) == webview::MessageError::None);
    lifetime.close();
    assert(lifetime.resultFor(secondDocument) == webview::MessageError::Closed);
    lifetime.close();
    assert(lifetime.resultFor(lifetime.token()) == webview::MessageError::Closed);

    auto state = std::make_shared<webview::WebViewState>();
    assert(state->initializationState() == webview::InitializationState::Initializing);
    bool queued = false;
    state->runWhenReady([&](const webview::InitializationResult& result) {
        queued = result.state == webview::InitializationState::Ready;
    });
    bool initialized = false;
    state->whenInitialized([&](const webview::InitializationResult& result) {
        initialized = result.state == webview::InitializationState::Ready;
    });
    state->markReady();
    assert(initialized);
    assert(queued);
    assert(state->initializationState() == webview::InitializationState::Ready);

    auto failedState = std::make_shared<webview::WebViewState>();
    bool failed = false;
    failedState->whenInitialized([&](const webview::InitializationResult& result) {
        failed = result.state == webview::InitializationState::Failed
            && !result.error.isEmpty();
    });
    failedState->failInitialization(QStringLiteral("test failure"));
    assert(failed);

    bool queuedFailure = false;
    failedState->runWhenReady([&](const webview::InitializationResult& result) {
        queuedFailure = result.state == webview::InitializationState::Failed
            && result.error == QStringLiteral("test failure");
    });
    assert(queuedFailure);

    auto closingState = std::make_shared<webview::WebViewState>();
    bool queuedClose = false;
    closingState->runWhenReady([&](const webview::InitializationResult& result) {
        queuedClose = result.state == webview::InitializationState::Closed;
    });
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
    scheduler.runWhenReady([&](const webview::InitializationResult&) {
        operationOrder.push_back(QStringLiteral("second"));
    });
    scheduler.markReady();
    assert(operationOrder == QStringList({ QStringLiteral("first"), QStringLiteral("second"),
                                QStringLiteral("nested") }));

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
    const webview::FileSelectionRequest singleFileRequest { { }, { }, false, false };
    const auto selected = webview::normalizeFileSelectionResult(singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile }, { } });
    assert(selected.status == webview::FileSelectionStatus::Selected);
    const auto empty = webview::normalizeFileSelectionResult(singleFileRequest,
        { webview::FileSelectionStatus::Selected, { }, { } });
    assert(empty.status == webview::FileSelectionStatus::Cancelled);
    const auto multiple = webview::normalizeFileSelectionResult(singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedFile, selectedFile }, { } });
    assert(multiple.status == webview::FileSelectionStatus::InvalidResult);
    const auto directory = webview::normalizeFileSelectionResult(singleFileRequest,
        { webview::FileSelectionStatus::Selected, { selectedRoot.path() }, { } });
    assert(directory.status == webview::FileSelectionStatus::InvalidResult);

    QVector<webview::WebResourceMapping> mappings {
        { QUrl(QStringLiteral("app://demo")), selectedRoot.path() }
    };
    QString mappingError;
    assert(webview::validateResourceMappings(&mappings, &mappingError));
    assert(mappings.front().localDirectory == QFileInfo(selectedRoot.path()).canonicalFilePath());
    const auto* mapping = webview::findResourceMapping(
        mappings, QUrl(QStringLiteral("app://demo/selected.txt")));
    assert(mapping);
    assert(webview::resolveMappedResource(
               *mapping, QUrl(QStringLiteral("app://demo/selected.txt")), &mappingError)
        == QFileInfo(selectedFile).canonicalFilePath());
    assert(webview::resolveMappedResource(
               *mapping, QUrl(QStringLiteral("app://demo/%2e%2e/secret")), &mappingError)
        .isEmpty());
    assert(webview::resolveMappedResource(
               *mapping, QUrl(QStringLiteral("app://demo/missing.txt")), &mappingError)
        .isEmpty());
    QTemporaryDir outsideRoot;
    assert(outsideRoot.isValid());
    const auto outsideFile = outsideRoot.filePath(QStringLiteral("secret.txt"));
    QFile secret(outsideFile);
    assert(secret.open(QIODevice::WriteOnly));
    secret.close();
    const auto linkedFile = selectedRoot.filePath(QStringLiteral("linked.txt"));
    assert(QFile::link(outsideFile, linkedFile));
    if (QFileInfo(linkedFile).isSymLink()) {
        assert(webview::resolveMappedResource(
                   *mapping, QUrl(QStringLiteral("app://demo/linked.txt")), &mappingError)
            .isEmpty());
    }

    QVector<webview::WebResourceMapping> duplicateMappings {
        { QUrl(QStringLiteral("app://demo")), selectedRoot.path() },
        { QUrl(QStringLiteral("APP://DEMO")), selectedRoot.path() }
    };
    assert(!webview::validateResourceMappings(&duplicateMappings, &mappingError));
    QVector<webview::WebResourceMapping> invalidMappings {
        { QUrl(QStringLiteral("https://demo/path")), selectedRoot.path() }
    };
    assert(!webview::validateResourceMappings(&invalidMappings, &mappingError));

}
