#pragma once

#include <system_webview/system_webview.h>

#include "platform/android/AndroidSession.h"

#include <jni.h>

#include <cstdint>
#include <memory>
#include <string>

namespace webview
{
class WebViewState;

/// Per-view Android state. Every member is touched from the Java main thread:
/// the host adapter forwards all WebView callbacks there and the demo backend
/// rejects sessions created on a different thread.
class AndroidViewState final : public std::enable_shared_from_this<AndroidViewState>
{
public:
    AndroidViewState(JNIEnv* env, jobject hostView, std::shared_ptr<AndroidSessionState> session, SessionMode mode);
    ~AndroidViewState();

    void start();
    void close();
    bool isClosed() const { return closed_; }

    void attach();
    void detach();
    void open(WebApplicationPtr application, const std::string& route);
    void navigate(const Url& url);
    void loadDocument(const std::string& html, const Url& baseUrl);
    void stop();
    void reload();
    bool canGoBack() const;
    bool goBack();
    bool canGoForward() const;
    bool goForward();
    void setHostCallbacks(WebViewHostCallbacks callbacks);
    WebViewBridge& bridge();
    WebResourceManager& resources();

    // WebView callbacks forwarded by the Java host adapter.
    void onPageStarted(const std::string& assetUrl);
    void onPageCommitVisible(const std::string& assetUrl);
    void onPageFinished(const std::string& assetUrl);
    void onReceivedError(const std::string& assetUrl, int nativeCode, const std::string& description);
    int shouldOverrideUrlLoading(const std::string& assetUrl, bool mainFrame, bool userInitiated, bool redirect);
    void onMessage(const std::string& message, const std::string& sourceOrigin, bool mainFrame);
    void onRenderProcessGone(int nativeCode);
    jobjectArray intercept(JNIEnv* env, const std::string& assetUrl, bool mainFrame);

    bool postToPage(const std::string& message);

    std::shared_ptr<WebViewState> state() const { return state_; }
    std::uint64_t id() const { return id_; }

private:
    Url mapAssetUrl(const Url& assetUrl) const;
    std::string toAssetUrl(const Url& publicUrl) const;
    void prepareDocument(const Url& publicUrl);
    void installDocumentTransport();
    bool callLoadUrl(const std::string& assetUrl);
    bool requireReady(const Url& url, const InitializationResult& result);
    void fail(const std::string& error);

    std::uint64_t id_ = 0;
    jobject host_ = nullptr;
    std::shared_ptr<AndroidSessionState> session_;
    std::shared_ptr<WebViewState> state_;
    std::string assetOrigin_;
    SessionMode mode_ = SessionMode::Persistent;
    bool closed_ = false;
    bool prepared_ = false;
    bool committedEmitted_ = false;
    std::uint64_t failedNavigationId_ = 0;
};

class AndroidView final : public IWebView
{
public:
    AndroidView(std::shared_ptr<AndroidViewState> state);
    ~AndroidView() override;

    InitializationState initializationState() const override;
    void whenInitialized(InitializationCompletion completion) override;
    void attachNativeView() override;
    void detachNativeView() override;
    void open(WebApplicationPtr application, const std::string& route) override;
    void navigate(const Url& url) override;
    void loadDocument(const std::string& html, const Url& baseUrl) override;
    void stop() override;
    void reload() override;
    bool canGoBack() const override;
    bool canGoForward() const override;
    void goBack() override;
    void goForward() override;
    void close() override;
    bool isClosed() const override;
    WebViewBridge& bridge() override;
    WebResourceManager& resources() override;
    void setHostCallbacks(WebViewHostCallbacks callbacks) override;

private:
    std::shared_ptr<AndroidViewState> state_;
};
} // namespace webview
