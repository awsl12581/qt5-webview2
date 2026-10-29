#include <system_webview/system_webview.h>

#include <gtest/gtest.h>

TEST(WindowsContract, UnsupportedFileSelectionIsExplicit)
{
    auto session = webview::createWebViewSession({});
    ASSERT_TRUE(session);
    ASSERT_TRUE(!session->supports(webview::WebViewCapability::FileSelection));
}
