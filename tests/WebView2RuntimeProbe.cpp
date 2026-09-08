#include "webview/IWebView.h"
#include "webview/WebViewFactory.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstdio>
#include <memory>

namespace
{
using HRESULT = long;
using PCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
extern "C" HRESULT __stdcall GetAvailableCoreWebView2BrowserVersionString(
    PCWSTR browserExecutableFolder, LPWSTR* versionInfo);
extern "C" void __stdcall CoTaskMemFree(void* memory);

const char* architecture()
{
#if defined(_M_ARM64)
    return "arm64";
#elif defined(_M_X64)
    return "x64";
#else
    return "unknown";
#endif
}

QString runtimeVersion()
{
    LPWSTR version = nullptr;
    const HRESULT result = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    if (result < 0) return QStringLiteral("unavailable:hresult=0x%1").arg(QString::number(static_cast<quint32>(result), 16));
    const QString value = QString::fromWCharArray(version ? version : L"");
    CoTaskMemFree(version);
    return value;
}

enum class ClearKind { Cache, Cookies, WebsiteData };

struct ProbeCase {
    webview::SessionMode mode;
    ClearKind clearKind;
};

class RuntimeProbe final : public std::enable_shared_from_this<RuntimeProbe>
{
public:
    explicit RuntimeProbe(QApplication& application)
        : application(application)
    {
        parent.setWindowTitle(QStringLiteral("WebView2 Runtime Probe (not the demo)"));
        parent.resize(640, 480);
        parent.show();
    }

    bool isReady() const { return profile.isValid(); }

    void start()
    {
        if (verifyRetainedViewClose()) runCase();
    }

private:
    bool verifyRetainedViewClose()
    {
        auto closingSession = webview::createWebViewSession({ });
        auto retainedView = closingSession->createWebView(&parent);
        closingSession.reset();
        if (!retainedView->isClosed()) {
            fail(QStringLiteral("A retained WebView2 view remained active after its session was destroyed."));
            return false;
        }
        bool completionCalled = false;
        retainedView->sendMessage({ 1, QStringLiteral("closed-probe"), {} },
            [&completionCalled](const webview::MessageResult& result) {
                completionCalled = result.error == webview::MessageError::Closed;
            });
        if (!completionCalled) {
            fail(QStringLiteral("A retained WebView2 view accepted work after its session was destroyed."));
            return false;
        }
        std::printf("session-close=ok retained-view=closed\n");
        std::fflush(stdout);
        return true;
    }

    void armTimeout(const QString& operation)
    {
        const auto expectedGeneration = ++generation;
        QTimer::singleShot(15000, &application,
            [weak = weak_from_this(), expectedGeneration, operation] {
                const auto owner = weak.lock();
                if (owner && owner->generation == expectedGeneration) {
                    owner->fail(QStringLiteral("Timed out waiting for WebView2 %1.").arg(operation));
                }
            });
    }

    void runCase()
    {
        if (caseIndex == cases.size()) {
            verifyClosingRace();
            return;
        }
        const auto current = cases[caseIndex];
        webview::WebViewSessionOptions options;
        options.mode = current.mode;
        if (current.mode == webview::SessionMode::Persistent) {
            options.profilePath = profile.path() + QStringLiteral("/%1")
                .arg(static_cast<int>(caseIndex));
        }
        session = webview::createWebViewSession(std::move(options));
        armTimeout(QStringLiteral("session initialization"));
        session->whenInitialized([weak = weak_from_this()](const webview::InitializationResult& result) {
            const auto owner = weak.lock();
            if (!owner) return;
            ++owner->generation;
            if (result.state != webview::InitializationState::Ready) {
                owner->fail(QStringLiteral("WebView2 session failed: %1").arg(result.error));
                return;
            }
            owner->createController();
        });
    }

    void createController()
    {
        const auto current = cases[caseIndex];
        if (current.mode == webview::SessionMode::Ephemeral
            && session->capabilitySupport(webview::WebViewCapability::PrivateProfile)
                != webview::CapabilitySupport::Supported) {
            fail(QStringLiteral("WebView2 Runtime does not expose InPrivate controller options."));
            return;
        }
        view = session->createWebView(&parent);
        view->attachNativeView();
        armTimeout(QStringLiteral("controller/profile initialization"));
        view->whenInitialized([weak = weak_from_this()](const webview::InitializationResult& result) {
            const auto owner = weak.lock();
            if (!owner) return;
            ++owner->generation;
            if (result.state != webview::InitializationState::Ready) {
                owner->fail(QStringLiteral("WebView2 controller/profile failed: %1").arg(result.error));
                return;
            }
            QTimer::singleShot(0, &owner->application,
                [weak] {
                    if (const auto current = weak.lock()) current->activateProfile();
                });
        });
    }

    void activateProfile()
    {
        QTimer::singleShot(500, &application,
            [weak = weak_from_this()] {
                if (const auto owner = weak.lock()) owner->clearData();
            });
    }

    void clearData()
    {
        const auto current = cases[caseIndex];
        const char* clearName = current.clearKind == ClearKind::Cache
            ? "cache"
            : current.clearKind == ClearKind::Cookies ? "cookies" : "website-data";
        std::printf("mode=%s controller=ready clearing=%s\n",
            current.mode == webview::SessionMode::Ephemeral ? "ephemeral" : "persistent",
            clearName);
        std::fflush(stdout);
        armTimeout(QStringLiteral("%1 clear completion").arg(QString::fromLatin1(clearName)));
        auto completion = [weak = weak_from_this(), clearName](const webview::WebsiteDataResult& result) {
            const auto owner = weak.lock();
            if (!owner) return;
            ++owner->generation;
            if (!result.success) {
                owner->fail(QStringLiteral("WebView2 %1 clear failed: %2")
                                .arg(QString::fromLatin1(clearName), result.error));
                return;
            }
            owner->finishCase(clearName);
        };
        QTimer::singleShot(3000, &application, [this, current, completion = std::move(completion)]() mutable {
            if (current.clearKind == ClearKind::Cache) session->clearCache(std::move(completion));
            else if (current.clearKind == ClearKind::Cookies) session->clearCookies(std::move(completion));
            else session->clearWebsiteData(std::move(completion));
        });
    }

    void finishCase(const char* clearName)
    {
        const auto current = cases[caseIndex];
        std::printf("mode=%s state=ready profile=ready clear=%s:ok\n",
            current.mode == webview::SessionMode::Ephemeral ? "ephemeral" : "persistent",
            clearName);
        std::fflush(stdout);
        if (view) view->close();
        view.reset();
        session.reset();
        ++caseIndex;
        QTimer::singleShot(100, &application,
            [weak = weak_from_this()] {
                if (const auto owner = weak.lock()) owner->runCase();
            });
    }

    void verifyClosingRace()
    {
        webview::WebViewSessionOptions options;
        options.mode = webview::SessionMode::Persistent;
        options.profilePath = profile.path() + QStringLiteral("/closing");
        auto closingSession = webview::createWebViewSession(std::move(options));
        auto retainedView = closingSession->createWebView(&parent);
        closingSession.reset();
        if (!retainedView->isClosed()) {
            fail(QStringLiteral("A retained WebView2 view remained active after its session was destroyed."));
            return;
        }
        bool completionCalled = false;
        retainedView->sendMessage({ 1, QStringLiteral("closed-probe"), {} },
            [&completionCalled](const webview::MessageResult& result) {
                completionCalled = result.error == webview::MessageError::Closed;
            });
        if (!completionCalled) {
            fail(QStringLiteral("A retained WebView2 view accepted work after its session was destroyed."));
            return;
        }
        retainedView.reset();
        ++generation;
        parent.close();
        std::printf("result=passed architecture=%s scenarios=%zu exit_code=0\n", architecture(), cases.size());
        std::fflush(stdout);
        QTimer::singleShot(250, &application, &QApplication::quit);
    }

    void fail(const QString& message)
    {
        if (failed) return;
        failed = true;
        ++generation;
        if (view) view->close();
        view.reset();
        session.reset();
        parent.close();
        std::fprintf(stderr, "result=failed architecture=%s scenario=%zu exit_code=1 detail=%s\n",
            architecture(), caseIndex, message.toUtf8().constData());
        std::fflush(stderr);
        application.exit(1);
    }

    QApplication& application;
    QTemporaryDir profile;
    QWidget parent;
    webview::WebViewSessionPtr session;
    webview::WebViewPtr view;
    std::size_t caseIndex = 0;
    quint64 generation = 0;
    bool failed = false;
    const std::array<ProbeCase, 4> cases { {
        { webview::SessionMode::Persistent, ClearKind::Cache },
        { webview::SessionMode::Persistent, ClearKind::Cookies },
        { webview::SessionMode::Persistent, ClearKind::WebsiteData },
        { webview::SessionMode::Ephemeral, ClearKind::WebsiteData },
    } };
};
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    std::printf("probe=webview2-runtime architecture=%s runtime_version=%s timeout_ms=15000\n",
        architecture(), runtimeVersion().toUtf8().constData());
    std::fflush(stdout);
    auto probe = std::make_shared<RuntimeProbe>(application);
    if (!probe->isReady()) {
        std::fprintf(stderr, "Unable to create the persistent test profile.\n");
        return 1;
    }
    QTimer::singleShot(0, &application, [probe] { probe->start(); });
    return application.exec();
}
