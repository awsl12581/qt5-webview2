#include "internal/TextEncoding.h"
#include "webview/HostCompletion.h"
#include "webview/WebViewState.h"
#include <system_webview/system_webview.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

namespace
{
void testHostCompletionGuardIsThreadSafe()
{
    auto state = std::make_shared<webview::WebViewState>();
    auto guard = std::make_shared<webview::HostCompletionGuard>(state);
    webview::HostCompletionAccess workerAccess;
    std::thread worker([&] { workerAccess = guard->claim(); });
    worker.join();
    ASSERT_TRUE(workerAccess.claim == webview::HostCompletionClaim::Accepted);
    ASSERT_TRUE(guard->claim().claim == webview::HostCompletionClaim::Duplicate);

    auto closedState = std::make_shared<webview::WebViewState>();
    closedState->close();
    webview::HostCompletionGuard closedGuard(closedState);
    ASSERT_TRUE(closedGuard.claim().claim == webview::HostCompletionClaim::OwnerUnavailable);
}

void testSnapshotDeletionRetriesAfterFileUnlock()
{
    const auto sourceDirectory = std::filesystem::temp_directory_path() / "system-webview-contract-test";
    std::filesystem::create_directories(sourceDirectory);
    const auto sourcePath = sourceDirectory / "resource.bin";
    std::ofstream(sourcePath) << "resource";

    auto state = std::make_shared<webview::WebViewState>();
    const webview::Url origin("https://trusted.example");
    state->setResourceContext(webview::Url("app://resource-test"), origin, "doc-1");
    const auto published = state->resources->publishFile(sourcePath.string());
    ASSERT_FALSE(published.token.empty());
    auto response = state->resources->open({ published.url, origin, "doc-1" });
    ASSERT_TRUE(response.status == 200);
    const auto snapshotPath = response.body->path();
    response.body.reset();
    response.lease.reset();

    const auto path = webview::utf8ToWide(snapshotPath);
    ASSERT_TRUE(path);
    HANDLE lockedFile = CreateFileW(path->c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_TRUE(lockedFile != INVALID_HANDLE_VALUE);
    state->resources->release(published.token);
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));
    ASSERT_EQ(state->resources->open({ published.url, origin, "doc-1" }).status, 410);
    CloseHandle(lockedFile);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));
    std::filesystem::remove_all(sourceDirectory);
}
}

TEST(WindowsContract, ResourceAndCompletionBehavior)
{
    testHostCompletionGuardIsThreadSafe();
    testSnapshotDeletionRetriesAfterFileUnlock();
}
