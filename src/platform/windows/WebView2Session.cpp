#include "platform/windows/WebView2Session.h"

#include "internal/Application.h"
#include "internal/Diagnostics.h"
#include "internal/HostView.h"
#include "internal/ResourceMapping.h"
#include "internal/TextEncoding.h"
#include "platform/windows/WebView2View.h"
#include "webview/WebViewState.h"

#include <unordered_set>

#include <WebView2.h>
#if defined(_MSC_VER) && defined(__has_attribute)
#undef __has_attribute
#endif
#include <WebView2EnvironmentOptions.h>
#include <windows.h>
#include <wrl.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace webview
{
namespace
{
std::string hresultError(std::string_view operation, HRESULT result)
{
    return std::format("{} failed (HRESULT 0x{:x}).", operation, static_cast<std::uint32_t>(result));
}

void finishClear(const std::shared_ptr<WebView2Session::ClearCompletion>& completion, bool success, std::string error = {})
{
    if (!completion || !*completion) {
        return;
    }
    auto callback = std::move(*completion);
    *completion = {};
    callback({ success, std::move(error) });
}
}

class WebView2Session::Impl
{
public:
    struct AsyncState
    {
        std::shared_ptr<WebViewState> scheduler = std::make_shared<WebViewState>(DiagnosticScope::Session);
        std::shared_ptr<WebViewState> profileScheduler = std::make_shared<WebViewState>();
        Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
        Microsoft::WRL::ComPtr<ICoreWebView2Environment5> environment5;
        Microsoft::WRL::ComPtr<ICoreWebView2Profile2> profile;
        EventRegistrationToken browserProcessExitedToken {};
        WebViewSessionHostCallbacks callbacks;
        std::vector<std::function<void()>> closeCallbacks;
        bool closed = false;
    };

    explicit Impl(WebViewSessionOptions options, WebViewPolicyPtr policy)
        : options(std::move(options))
        , policy(std::move(policy))
        , async(std::make_shared<AsyncState>())
    {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ownsApartment = SUCCEEDED(apartment);
        if (!ownsApartment) {
            async->scheduler->failInitialization(
                std::format("WebView2 requires a COM STA UI thread (HRESULT 0x{:x}).", static_cast<std::uint32_t>(apartment)));
            return;
        }
        if (this->options.mode == SessionMode::Persistent && this->options.profilePath.empty()) {
            async->scheduler->failInitialization("Persistent WebView2 sessions require a profilePath.");
            return;
        }
        std::filesystem::path profilePath;
        if (this->options.mode == SessionMode::Persistent) {
            const auto nativePath = utf8ToWide(this->options.profilePath);
            if (!nativePath) {
                async->scheduler->failInitialization("Invalid WebView2 profile path encoding.");
                return;
            }
            profilePath = std::filesystem::path(*nativePath);
            std::error_code error;
            std::filesystem::create_directories(profilePath, error);
            if (error) {
                async->scheduler->failInitialization("WebView2 profile is not writable: " + error.message());
                return;
            }
        }
        Microsoft::WRL::ComPtr<CoreWebView2EnvironmentOptions> environmentOptions;
        Microsoft::WRL::ComPtr<CoreWebView2CustomSchemeRegistration> appScheme;
        environmentOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        appScheme = Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(L"app");
        if (!environmentOptions || !appScheme || FAILED(appScheme->put_TreatAsSecure(TRUE))
            || FAILED(appScheme->put_HasAuthorityComponent(TRUE))) {
            async->scheduler->failInitialization("WebView2 app custom-scheme options are unavailable.");
            return;
        }
        ICoreWebView2CustomSchemeRegistration* schemes[] = { appScheme.Get() };
        const HRESULT registration = environmentOptions->SetCustomSchemeRegistrations(1, schemes);
        if (FAILED(registration)) {
            async->scheduler->failInitialization(hresultError("WebView2 app custom-scheme registration", registration));
            return;
        }
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            profilePath.empty() ? nullptr : profilePath.c_str(),
            environmentOptions.Get(),
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weak = std::weak_ptr<AsyncState>(async)](HRESULT hr, ICoreWebView2Environment* created) -> HRESULT {
                    const auto owner = weak.lock();
                    if (!owner || owner->closed) {
                        return S_OK;
                    }
                    if (FAILED(hr) || !created) {
                        const auto error = hresultError("WebView2 Runtime environment creation", hr);
                        owner->scheduler->failInitialization(error);
                        owner->profileScheduler->failInitialization(error);
                        return S_OK;
                    }
                    owner->environment = created;
                    if (SUCCEEDED(owner->environment.As(&owner->environment5)) && owner->environment5) {
                        owner->environment5->add_BrowserProcessExited(
                            Microsoft::WRL::Callback<ICoreWebView2BrowserProcessExitedEventHandler>(
                                [weak](ICoreWebView2Environment*, ICoreWebView2BrowserProcessExitedEventArgs* args) -> HRESULT {
                                    const auto current = weak.lock();
                                    if (!current || current->closed || !args) {
                                        return S_OK;
                                    }
                                    COREWEBVIEW2_BROWSER_PROCESS_EXIT_KIND kind = COREWEBVIEW2_BROWSER_PROCESS_EXIT_KIND_NORMAL;
                                    UINT32 processId = 0;
                                    args->get_BrowserProcessExitKind(&kind);
                                    args->get_BrowserProcessId(&processId);
                                    if (kind != COREWEBVIEW2_BROWSER_PROCESS_EXIT_KIND_FAILED) {
                                        return S_OK;
                                    }
                                    const RuntimeFailureEvent event {
                                        RuntimeFailureKind::BrowserProcessTerminated,
                                        "The WebView2 browser process terminated unexpectedly.",
                                        static_cast<std::int64_t>(processId),
                                    };
                                    diagnosticLog(DiagnosticLevel::Critical, "system_webview.runtime")
                                        << "event=runtime.browser_process_terminated"
                                        << "session=" << current->scheduler->diagnosticId << "process=" << processId;
                                    if (current->callbacks.onRuntimeFailure) {
                                        current->callbacks.onRuntimeFailure(event);
                                    }
                                    return S_OK;
                                })
                                .Get(),
                            &owner->browserProcessExitedToken);
                    }
                    owner->scheduler->markReady();
                    return S_OK;
                })
                .Get());
        if (FAILED(result)) {
            const auto error = std::format("WebView2 Runtime is unavailable (HRESULT 0x{:x}).", static_cast<std::uint32_t>(result));
            async->scheduler->failInitialization(error);
            async->profileScheduler->failInitialization(error);
        }
    }

    void clearBrowsingData(COREWEBVIEW2_BROWSING_DATA_KINDS kind, WebView2Session::ClearCompletion completion)
    {
        auto sharedCompletion = std::make_shared<WebView2Session::ClearCompletion>(std::move(completion));
        async->profileScheduler->runWhenReady(
            [weak = std::weak_ptr<AsyncState>(async), kind, sharedCompletion](const InitializationResult& initialization) {
                const auto owner = weak.lock();
                if (!owner || owner->closed) {
                    finishClear(sharedCompletion, false, "The WebView2 session is closed.");
                    return;
                }
                if (initialization.state != InitializationState::Ready) {
                    finishClear(sharedCompletion, false, initialization.error);
                    return;
                }
                if (!owner->profile) {
                    finishClear(sharedCompletion, false, "WebView2 profile is unavailable until a web view controller is ready.");
                    return;
                }
                auto completed = Microsoft::WRL::Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
                    [weak, sharedCompletion](HRESULT result) -> HRESULT {
                        const auto current = weak.lock();
                        if (!current || current->closed) {
                            finishClear(sharedCompletion, false, "The WebView2 session is closed.");
                        }
                        else {
                            finishClear(
                                sharedCompletion,
                                SUCCEEDED(result),
                                SUCCEEDED(result) ? std::string() : hresultError("WebView2 browsing-data clear", result));
                        }
                        return S_OK;
                    });
                const HRESULT result = owner->profile->ClearBrowsingData(kind, completed.Get());
                if (FAILED(result)) {
                    finishClear(sharedCompletion, false, hresultError("WebView2 browsing-data clear request", result));
                }
            });
    }

    WebViewSessionOptions options;
    std::shared_ptr<std::vector<ResourceMapping>> resourceMappings = std::make_shared<std::vector<ResourceMapping>>();
    std::unordered_set<std::string> applicationIds;
    std::vector<WebApplicationPtr> applications;
    WebViewPolicyPtr policy;
    std::shared_ptr<AsyncState> async;
    bool ownsApartment = false;
};

WebView2Session::WebView2Session(WebViewSessionOptions options, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(policy)))
{
}

WebView2Session::~WebView2Session()
{
    if (impl_) {
        impl_->async->closed = true;
        impl_->async->callbacks = {};
        if (impl_->async->environment5) {
            impl_->async->environment5->remove_BrowserProcessExited(impl_->async->browserProcessExitedToken);
        }
        impl_->async->scheduler->close();
        impl_->async->profileScheduler->close();
        auto callbacks = std::move(impl_->async->closeCallbacks);
        for (auto& callback : callbacks) {
            if (callback) {
                callback();
            }
        }
        impl_->async->profile.Reset();
        impl_->async->environment5.Reset();
        impl_->async->environment.Reset();
    }
    if (impl_ && impl_->ownsApartment) {
        CoUninitialize();
    }
}

InitializationState WebView2Session::initializationState() const
{
    return impl_->async->scheduler->initializationState();
}

void WebView2Session::whenInitialized(InitializationCompletion completion)
{
    impl_->async->scheduler->whenInitialized(std::move(completion));
}

WebApplicationPtr WebView2Session::createApplication(WebApplicationOptions options)
{
    std::string error;
    const auto application = webview::createApplication(std::move(options), &error);
    if (!application || impl_->applicationIds.contains(application->id())) {
        return {};
    }
    if (const auto* bundle = std::get_if<LocalBundle>(&application->source())) {
        std::vector<ResourceMapping> candidate = *impl_->resourceMappings;
        candidate.push_back(
            { application->origin(), bundle->directory, bundle->entryDocument, bundle->spaFallback, bundle->externalNetworkAccess });
        if (!validateResourceMappings(&candidate, &error)
            || resolveMappedResource(candidate.back(), application->urlForRoute({}), &error).empty()) {
            return {};
        }
        *impl_->resourceMappings = std::move(candidate);
    }
    impl_->applicationIds.insert(application->id());
    impl_->applications.push_back(application);
    return application;
}

WebViewPtr WebView2Session::createWebView(WebViewHost host)
{
    auto* hostView = new ExternalHostView(std::move(host));
    const auto expectedMode = impl_->options.mode;
    auto registerProfile = [weak = std::weak_ptr<Impl::AsyncState>(impl_->async), expectedMode](ICoreWebView2* webview) -> std::string {
        const auto owner = weak.lock();
        if (!owner || owner->closed) {
            return "The WebView2 session is closed.";
        }
        Microsoft::WRL::ComPtr<ICoreWebView2_13> webview13;
        Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile;
        Microsoft::WRL::ComPtr<ICoreWebView2Profile2> profile2;
        HRESULT result = webview ? webview->QueryInterface(IID_PPV_ARGS(&webview13)) : E_POINTER;
        if (SUCCEEDED(result)) {
            result = webview13->get_Profile(&profile);
        }
        if (SUCCEEDED(result)) {
            result = profile.As(&profile2);
        }
        if (FAILED(result) || !profile2) {
            return hresultError("WebView2 profile browsing-data interface", result);
        }
        BOOL isInPrivate = FALSE;
        result = profile2->get_IsInPrivateModeEnabled(&isInPrivate);
        if (FAILED(result)) {
            return hresultError("WebView2 profile mode query", result);
        }
        const bool expectedPrivate = expectedMode == SessionMode::Ephemeral;
        if ((isInPrivate != FALSE) != expectedPrivate) {
            return "WebView2 returned a profile mode that does not match the requested session mode.";
        }
        owner->profile = std::move(profile2);
        owner->profileScheduler->markReady();
        return {};
    };
    auto registerSessionClose = [weak = std::weak_ptr<Impl::AsyncState>(impl_->async)](std::function<void()> callback) {
        const auto owner = weak.lock();
        if (!owner || owner->closed) {
            if (callback) {
                callback();
            }
            return;
        }
        owner->closeCallbacks.push_back(std::move(callback));
    };
    auto environmentProvider = [weak = std::weak_ptr<Impl::AsyncState>(impl_->async)]() -> ICoreWebView2Environment* {
        const auto owner = weak.lock();
        return owner && !owner->closed ? owner->environment.Get() : nullptr;
    };
    return std::unique_ptr<IWebView>(new WebView2View(
        hostView,
        ownsHost,
        std::move(environmentProvider),
        impl_->async->scheduler,
        impl_->policy,
        impl_->resourceMappings,
        impl_->options.mode,
        std::move(registerProfile),
        std::move(registerSessionClose)));
}

void WebView2Session::clearCache(ClearCompletion completion)
{
    impl_->clearBrowsingData(COREWEBVIEW2_BROWSING_DATA_KINDS_DISK_CACHE, std::move(completion));
}

void WebView2Session::clearCookies(ClearCompletion completion)
{
    impl_->clearBrowsingData(COREWEBVIEW2_BROWSING_DATA_KINDS_COOKIES, std::move(completion));
}

void WebView2Session::clearWebsiteData(ClearCompletion completion)
{
    impl_->clearBrowsingData(COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_SITE, std::move(completion));
}

void WebView2Session::setHostCallbacks(WebViewSessionHostCallbacks callbacks)
{
    if (!impl_->async->closed) {
        impl_->async->callbacks = std::move(callbacks);
    }
}

bool WebView2Session::supports(WebViewCapability capability) const
{
    if (capability == WebViewCapability::FileSelection) {
        // TODO(webview2-file-selection): revisit when WebView2 exposes a host chooser event.
        return false;
    }
    if (capability == WebViewCapability::PersistentProfile || capability == WebViewCapability::DownloadDefault
        || capability == WebViewCapability::DownloadTarget) {
        return true;
    }
    if (capability == WebViewCapability::PrivateProfile) {
        Microsoft::WRL::ComPtr<ICoreWebView2Environment10> environment10;
        return impl_->async->environment && SUCCEEDED(impl_->async->environment.As(&environment10));
    }
    return false;
}
} // namespace webview
