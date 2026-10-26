#include <system_webview/system_webview.h>

#include <Cocoa/Cocoa.h>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

namespace
{
class TempDir final
{
public:
    TempDir()
    {
        const auto root = std::filesystem::temp_directory_path();
        path_ = root / ("system-webview-session-" + std::to_string(std::hash<std::thread::id> {}(std::this_thread::get_id())));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    std::string path() const { return path_.string(); }
    std::string filePath(const char* name) const { return (path_ / name).string(); }

private:
    std::filesystem::path path_;
};

bool waitForClear(const std::function<void(webview::IWebViewSession::ClearCompletion)>& clear)
{
    bool completed = false;
    bool succeeded = false;
    clear([&](const webview::WebsiteDataResult& result) {
        completed = true;
        succeeded = result.success;
    });
    const auto deadline = [NSDate dateWithTimeIntervalSinceNow:15.0];
    while (!completed && [deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    return completed && succeeded;
}
}

TEST(MacOSSession, SessionCapabilitiesAndDataClearing)
{
    TempDir profile;
    ASSERT_FALSE(profile.path().empty());

    webview::WebViewSessionOptions persistentOptions;
    persistentOptions.mode = webview::SessionMode::Persistent;
    persistentOptions.profilePath = profile.path();
    auto session = webview::createWebViewSession(std::move(persistentOptions));
    auto ephemeral = webview::createWebViewSession({});
    webview::WebApplicationOptions invalidBundle;
    invalidBundle.id = "demo";
    invalidBundle.source = webview::LocalBundle { profile.path(), "../index.html" };
    ASSERT_FALSE(session->createApplication(std::move(invalidBundle)));
    std::ofstream(profile.filePath("index.html")).close();
    webview::WebApplicationOptions bundle;
    bundle.id = "bundle";
    bundle.source = webview::LocalBundle { profile.path() };
    ASSERT_TRUE(session->createApplication(bundle));
    ASSERT_TRUE(!session->createApplication(std::move(bundle)));
    webview::WebApplicationOptions unicodeBundle;
    unicodeBundle.id = "应用-\U00020000";
    unicodeBundle.source = webview::LocalBundle { profile.path() };
    const auto unicodeApplication = session->createApplication(unicodeBundle);
    ASSERT_TRUE(unicodeApplication);
    ASSERT_TRUE(!session->createApplication(std::move(unicodeBundle)));
    ASSERT_TRUE(session->supports(webview::WebViewCapability::PersistentProfile));
    ASSERT_TRUE(ephemeral->supports(webview::WebViewCapability::PersistentProfile));
    ASSERT_TRUE(ephemeral->supports(webview::WebViewCapability::PrivateProfile));
    ASSERT_TRUE(session->supports(webview::WebViewCapability::PrivateProfile));
    ASSERT_TRUE(session->supports(webview::WebViewCapability::FileSelection));
    ASSERT_TRUE(!session->supports(webview::WebViewCapability::Location));
    ASSERT_TRUE(!session->supports(webview::WebViewCapability::Notifications));
    ASSERT_TRUE(!session->supports(webview::WebViewCapability::Clipboard));
    if (@available(macOS 12.0, *)) {
        ASSERT_TRUE(session->supports(webview::WebViewCapability::Camera));
        ASSERT_TRUE(session->supports(webview::WebViewCapability::Microphone));
    }
    if (@available(macOS 11.3, *)) {
        ASSERT_TRUE(session->supports(webview::WebViewCapability::DownloadTarget));
    }

    ASSERT_TRUE(waitForClear([&](auto completion) { session->clearCache(std::move(completion)); }));
    ASSERT_TRUE(waitForClear([&](auto completion) { session->clearCookies(std::move(completion)); }));
    ASSERT_TRUE(waitForClear([&](auto completion) { session->clearWebsiteData(std::move(completion)); }));
}
