#include "platform/windows/WebView2Session.h"

#include "platform/windows/WebView2View.h"
#include "internal/ResourceMapping.h"
#include "internal/Application.h"
#include "webview/WebViewState.h"

#include <QWidget>
#include <QSet>

#include <WebView2.h>
#if defined(_MSC_VER) && defined(__has_attribute)
#undef __has_attribute
#endif
#include <WebView2EnvironmentOptions.h>
#include <windows.h>
#include <wrl.h>

#include <filesystem>

namespace webview
{
namespace {
QString hresultError(const QString& operation, HRESULT result)
{
    return QStringLiteral("%1 failed (HRESULT 0x%2).")
        .arg(operation, QString::number(static_cast<quint32>(result), 16));
}

void finishClear(const std::shared_ptr<WebView2Session::ClearCompletion>& completion,
    bool success, QString error = { })
{
    if (!completion || !*completion) return;
    auto callback = std::move(*completion);
    *completion = {};
    callback({ success, std::move(error) });
}
}

class WebView2Session::Impl
{
public:
    struct AsyncState {
        std::shared_ptr<WebViewState> scheduler = std::make_shared<WebViewState>();
        std::shared_ptr<WebViewState> profileScheduler = std::make_shared<WebViewState>();
        Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
        Microsoft::WRL::ComPtr<ICoreWebView2Profile2> profile;
        std::vector<std::function<void()>> closeCallbacks;
        bool closed = false;
    };

    explicit Impl(WebViewSessionOptions options, WebViewPolicyPtr policy)
        : options(std::move(options)), policy(std::move(policy)), async(std::make_shared<AsyncState>())
    {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ownsApartment = SUCCEEDED(apartment);
        if (!ownsApartment) {
            async->scheduler->failInitialization(QStringLiteral("WebView2 requires a COM STA UI thread (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(apartment), 16)));
            return;
        }
        if (this->options.mode == SessionMode::Persistent && this->options.profilePath.isEmpty()) {
            async->scheduler->failInitialization(QStringLiteral("Persistent WebView2 sessions require a profilePath."));
            return;
        }
        std::filesystem::path profilePath;
        if (this->options.mode == SessionMode::Persistent) {
            profilePath = std::filesystem::path(this->options.profilePath.toStdWString());
            std::error_code error;
            std::filesystem::create_directories(profilePath, error);
            if (error) {
                async->scheduler->failInitialization(QStringLiteral("WebView2 profile is not writable: %1").arg(QString::fromStdString(error.message())));
                return;
            }
        }
        Microsoft::WRL::ComPtr<CoreWebView2EnvironmentOptions> environmentOptions;
        Microsoft::WRL::ComPtr<CoreWebView2CustomSchemeRegistration> appScheme;
        environmentOptions = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        appScheme = Microsoft::WRL::Make<CoreWebView2CustomSchemeRegistration>(L"app");
        if (!environmentOptions || !appScheme
            || FAILED(appScheme->put_TreatAsSecure(TRUE))
            || FAILED(appScheme->put_HasAuthorityComponent(TRUE))) {
            async->scheduler->failInitialization(QStringLiteral("WebView2 app custom-scheme options are unavailable."));
            return;
        }
        ICoreWebView2CustomSchemeRegistration* schemes[] = { appScheme.Get() };
        const HRESULT registration = environmentOptions->SetCustomSchemeRegistrations(1, schemes);
        if (FAILED(registration)) {
            async->scheduler->failInitialization(hresultError(QStringLiteral("WebView2 app custom-scheme registration"), registration));
            return;
        }
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, profilePath.empty() ? nullptr : profilePath.c_str(), environmentOptions.Get(),
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weak = std::weak_ptr<AsyncState>(async)](HRESULT hr, ICoreWebView2Environment* created) -> HRESULT {
                    const auto owner = weak.lock();
                    if (!owner || owner->closed) return S_OK;
                    if (FAILED(hr) || !created) {
                        const auto error = QStringLiteral("WebView2 Runtime environment creation failed (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(hr), 16));
                        owner->scheduler->failInitialization(error);
                        owner->profileScheduler->failInitialization(error);
                        return S_OK;
                    }
                    owner->environment = created;
                    owner->scheduler->markReady();
                    return S_OK;
                }).Get());
        if (FAILED(result)) {
            const auto error = QStringLiteral("WebView2 Runtime is unavailable (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(result), 16));
            async->scheduler->failInitialization(error);
            async->profileScheduler->failInitialization(error);
        }
    }

    void clearBrowsingData(COREWEBVIEW2_BROWSING_DATA_KINDS kind,
        WebView2Session::ClearCompletion completion)
    {
        auto sharedCompletion = std::make_shared<WebView2Session::ClearCompletion>(std::move(completion));
        async->profileScheduler->runWhenReady([weak = std::weak_ptr<AsyncState>(async),
                                           kind, sharedCompletion](const InitializationResult& initialization) {
            const auto owner = weak.lock();
            if (!owner || owner->closed) {
                finishClear(sharedCompletion, false, QStringLiteral("The WebView2 session is closed."));
                return;
            }
            if (initialization.state != InitializationState::Ready) {
                finishClear(sharedCompletion, false, initialization.error);
                return;
            }
            if (!owner->profile) {
                finishClear(sharedCompletion, false, QStringLiteral("WebView2 profile is unavailable until a web view controller is ready."));
                return;
            }
            auto completed = Microsoft::WRL::Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
                [weak, sharedCompletion](HRESULT result) -> HRESULT {
                    const auto current = weak.lock();
                    if (!current || current->closed) {
                        finishClear(sharedCompletion, false, QStringLiteral("The WebView2 session is closed."));
                    } else {
                        finishClear(sharedCompletion, SUCCEEDED(result), SUCCEEDED(result) ? QString() : hresultError(QStringLiteral("WebView2 browsing-data clear"), result));
                    }
                    return S_OK;
                });
            const HRESULT result = owner->profile->ClearBrowsingData(kind, completed.Get());
            if (FAILED(result)) finishClear(sharedCompletion, false, hresultError(QStringLiteral("WebView2 browsing-data clear request"), result));
        });
    }

    WebViewSessionOptions options;
    std::shared_ptr<QVector<ResourceMapping>> resourceMappings = std::make_shared<QVector<ResourceMapping>>();
    QSet<QString> applicationIds;
    QVector<WebApplicationPtr> applications;
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
        impl_->async->scheduler->close();
        impl_->async->profileScheduler->close();
        auto callbacks = std::move(impl_->async->closeCallbacks);
        for (auto& callback : callbacks) if (callback) callback();
        impl_->async->profile.Reset();
        impl_->async->environment.Reset();
    }
    if (impl_ && impl_->ownsApartment) {
        CoUninitialize();
    }
}

InitializationState WebView2Session::initializationState() const { return impl_->async->scheduler->initializationState(); }

void WebView2Session::whenInitialized(InitializationCompletion completion)
{
    impl_->async->scheduler->whenInitialized(std::move(completion));
}

WebApplicationPtr WebView2Session::createApplication(WebApplicationOptions options)
{
    QString error;
    const auto application = webview::createApplication(std::move(options), &error);
    if (!application || impl_->applicationIds.contains(application->id())) return { };
    if (const auto* bundle = std::get_if<LocalBundle>(&application->source())) {
        QVector<ResourceMapping> candidate = *impl_->resourceMappings;
        candidate.push_back({ application->origin(), bundle->directory, bundle->entryDocument, bundle->spaFallback });
        if (!validateResourceMappings(&candidate, &error)
            || resolveMappedResource(candidate.back(), application->urlForRoute({ }), &error).isEmpty()) return { };
        *impl_->resourceMappings = std::move(candidate);
    }
    impl_->applicationIds.insert(application->id());
    impl_->applications.push_back(application);
    return application;
}

WebViewPtr WebView2Session::createWebView(QWidget* parent)
{
    const auto expectedMode = impl_->options.mode;
    auto registerProfile = [weak = std::weak_ptr<Impl::AsyncState>(impl_->async), expectedMode](ICoreWebView2* webview) -> QString {
        const auto owner = weak.lock();
        if (!owner || owner->closed) return QStringLiteral("The WebView2 session is closed.");
        Microsoft::WRL::ComPtr<ICoreWebView2_13> webview13;
        Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile;
        Microsoft::WRL::ComPtr<ICoreWebView2Profile2> profile2;
        HRESULT result = webview ? webview->QueryInterface(IID_PPV_ARGS(&webview13)) : E_POINTER;
        if (SUCCEEDED(result)) result = webview13->get_Profile(&profile);
        if (SUCCEEDED(result)) result = profile.As(&profile2);
        if (FAILED(result) || !profile2) return hresultError(QStringLiteral("WebView2 profile browsing-data interface"), result);
        BOOL isInPrivate = FALSE;
        result = profile2->get_IsInPrivateModeEnabled(&isInPrivate);
        if (FAILED(result)) return hresultError(QStringLiteral("WebView2 profile mode query"), result);
        const bool expectedPrivate = expectedMode == SessionMode::Ephemeral;
        if ((isInPrivate != FALSE) != expectedPrivate) {
            return QStringLiteral("WebView2 returned a profile mode that does not match the requested session mode.");
        }
        owner->profile = std::move(profile2);
        owner->profileScheduler->markReady();
        return {};
    };
    auto registerSessionClose = [weak = std::weak_ptr<Impl::AsyncState>(impl_->async)](std::function<void()> callback) {
        const auto owner = weak.lock();
        if (!owner || owner->closed) {
            if (callback) callback();
            return;
        }
        owner->closeCallbacks.push_back(std::move(callback));
    };
    auto environmentProvider = [weak = std::weak_ptr<Impl::AsyncState>(impl_->async)]() -> ICoreWebView2Environment* {
        const auto owner = weak.lock();
        return owner && !owner->closed ? owner->environment.Get() : nullptr;
    };
    return std::unique_ptr<IWebView>(new WebView2View(parent, std::move(environmentProvider),
        impl_->async->scheduler, impl_->policy, impl_->resourceMappings,
        impl_->options.mode, std::move(registerProfile), std::move(registerSessionClose)));
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

CapabilitySupport WebView2Session::capabilitySupport(WebViewCapability capability) const
{
    if (capability == WebViewCapability::FileSelection) {
        // TODO(webview2-file-selection): revisit when WebView2 exposes a host chooser event.
        return CapabilitySupport::Unsupported;
    }
    if (capability == WebViewCapability::PersistentProfile
        || capability == WebViewCapability::DownloadDefault
        || capability == WebViewCapability::DownloadTarget) {
        return CapabilitySupport::Supported;
    }
    if (capability == WebViewCapability::PrivateProfile) {
        Microsoft::WRL::ComPtr<ICoreWebView2Environment10> environment10;
        return impl_->async->environment && SUCCEEDED(impl_->async->environment.As(&environment10))
            ? CapabilitySupport::Supported
            : CapabilitySupport::Unsupported;
    }
    return CapabilitySupport::Unsupported;
}
} // namespace webview
