#include "webview/HostCompletion.h"
#include "webview/WebViewState.h"
#include <system_webview/system_webview.h>

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <windows.h>

#include <cassert>
#include <thread>

namespace
{
void testUnsupportedFileSelectionIsExplicit()
{
    auto session = webview::createWebViewSession({ });
    assert(session);
    assert(!session->supports(webview::WebViewCapability::FileSelection));
}

void testHostCompletionGuardIsThreadSafe()
{
    auto state = std::make_shared<webview::WebViewState>();
    auto guard = std::make_shared<webview::HostCompletionGuard>(state);
    webview::HostCompletionAccess workerAccess;
    std::thread worker([&] { workerAccess = guard->claim(); });
    worker.join();
    assert(workerAccess.claim == webview::HostCompletionClaim::Accepted);
    assert(guard->claim().claim == webview::HostCompletionClaim::Duplicate);

    auto closedState = std::make_shared<webview::WebViewState>();
    closedState->close();
    webview::HostCompletionGuard closedGuard(closedState);
    assert(closedGuard.claim().claim == webview::HostCompletionClaim::OwnerUnavailable);
}

void testSnapshotDeletionRetriesAfterFileUnlock()
{
    QTemporaryDir sourceDirectory;
    assert(sourceDirectory.isValid());
    QFile source(sourceDirectory.filePath(QStringLiteral("resource.bin")));
    assert(source.open(QIODevice::WriteOnly));
    assert(source.write("resource") == 8);
    source.close();

    auto state = std::make_shared<webview::WebViewState>();
    const QUrl origin(QStringLiteral("https://trusted.example"));
    state->setResourceContext(QUrl(QStringLiteral("app://resource-test")), origin, QStringLiteral("doc-1"));
    const auto published = state->resources->publishFile(source.fileName());
    assert(!published.token.isEmpty());
    auto response = state->resources->open({ published.url, origin, QStringLiteral("doc-1") });
    assert(response.status == 200);
    const auto snapshotPath = qobject_cast<QFile*>(response.body.get())->fileName();
    response.body.reset();
    response.lease.reset();

    const auto path = snapshotPath.toStdWString();
    HANDLE lockedFile = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(lockedFile != INVALID_HANDLE_VALUE);
    state->resources->release(published.token);
    assert(QFileInfo::exists(snapshotPath));
    assert(state->resources->open({ published.url, origin, QStringLiteral("doc-1") }).status == 410);
    CloseHandle(lockedFile);

    QEventLoop loop;
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);
    loop.exec();
    assert(!QFileInfo::exists(snapshotPath));
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    testUnsupportedFileSelectionIsExplicit();
    testHostCompletionGuardIsThreadSafe();
    testSnapshotDeletionRetriesAfterFileUnlock();
    return 0;
}
