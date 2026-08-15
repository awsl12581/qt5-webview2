#include "platform/windows/WebView2Session.h"

#include "platform/windows/WebView2View.h"
#include "webview/WebViewState.h"

#include <QWidget>

#include <WebView2.h>
#include <windows.h>
#include <wrl.h>

#include <filesystem>

namespace webview
{
class WebView2Session::Impl
{
public:
    explicit Impl(WebViewSessionOptions options, WebViewPolicyPtr policy)
        : options(std::move(options)), policy(std::move(policy)), state(std::make_shared<WebViewState>())
    {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized = SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE;
        if (!comInitialized || apartment == RPC_E_CHANGED_MODE) {
            state->failInitialization(QStringLiteral("WebView2 requires a COM STA UI thread (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(apartment), 16)));
            return;
        }
        if (this->options.mode == SessionMode::Persistent && this->options.profilePath.isEmpty()) {
            state->failInitialization(QStringLiteral("Persistent WebView2 sessions require a profilePath."));
            return;
        }
        if (this->options.mode == SessionMode::Ephemeral) {
            state->failInitialization(QStringLiteral("WebView2 ephemeral profile requires a Runtime controller-options interface."));
            return;
        }
        const auto path = std::filesystem::path(this->options.profilePath.isEmpty()
                ? (std::filesystem::temp_directory_path() / "system-webview2-ephemeral")
                : std::filesystem::path(this->options.profilePath.toStdWString()));
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) {
            state->failInitialization(QStringLiteral("WebView2 profile is not writable: %1").arg(QString::fromStdString(error.message())));
            return;
        }
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, path.c_str(), nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [state = state, this](HRESULT hr, ICoreWebView2Environment* created) -> HRESULT {
                    if (FAILED(hr) || !created) {
                        state->failInitialization(QStringLiteral("WebView2 Runtime environment creation failed (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(hr), 16)));
                        return S_OK;
                    }
                    environment = created;
                    state->markReady();
                    return S_OK;
                }).Get());
        if (FAILED(result)) {
            state->failInitialization(QStringLiteral("WebView2 Runtime is unavailable (HRESULT 0x%1).").arg(QString::number(static_cast<quint32>(result), 16)));
        }
    }

    WebViewSessionOptions options;
    WebViewPolicyPtr policy;
    std::shared_ptr<WebViewState> state;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    bool comInitialized = false;
};

WebView2Session::WebView2Session(WebViewSessionOptions options, WebViewPolicyPtr policy)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(policy)))
{
}

WebView2Session::~WebView2Session()
{
    if (impl_ && impl_->comInitialized) {
        CoUninitialize();
    }
}

InitializationState WebView2Session::initializationState() const { return impl_->state->initializationState(); }

void WebView2Session::whenInitialized(InitializationCompletion completion)
{
    impl_->state->whenInitialized(std::move(completion));
}

WebViewPtr WebView2Session::createWebView(QWidget* parent)
{
    return std::unique_ptr<IWebView>(new WebView2View(parent, impl_->environment.Get(), impl_->state, impl_->policy, impl_->options.resourceMappings));
}

void WebView2Session::clearCache(ClearCompletion completion)
{
    impl_->state->runWhenReady([completion = std::move(completion)](const InitializationResult& result) {
        if (completion) completion({ result.state == InitializationState::Ready, result.error });
    });
}

void WebView2Session::clearCookies(ClearCompletion completion) { clearCache(std::move(completion)); }
void WebView2Session::clearWebsiteData(ClearCompletion completion) { clearCache(std::move(completion)); }

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
    if (capability == WebViewCapability::PrivateProfile
        || capability == WebViewCapability::ResourceMapping) {
        return CapabilitySupport::Unsupported;
    }
    return CapabilitySupport::Unsupported;
}
} // namespace webview
