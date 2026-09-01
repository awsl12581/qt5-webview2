#include "webview/HostCompletion.h"
#include "webview/WebViewFactory.h"
#include "webview/WebViewState.h"

#include <QApplication>

#include <cassert>
#include <thread>

namespace
{
void testUnsupportedFileSelectionIsExplicit()
{
    auto session = webview::createWebViewSession({ });
    assert(session);
    assert(session->capabilitySupport(webview::WebViewCapability::FileSelection)
        == webview::CapabilitySupport::Unsupported);
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
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    testUnsupportedFileSelectionIsExplicit();
    testHostCompletionGuardIsThreadSafe();
    return 0;
}
