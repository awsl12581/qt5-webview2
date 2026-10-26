#pragma once

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <system_webview/system_webview.h>

namespace webview
{
class DiagnosticStream final
{
public:
    DiagnosticStream(DiagnosticLevel level, std::string_view category);
    DiagnosticStream(DiagnosticStream&& other) noexcept;
    DiagnosticStream(const DiagnosticStream&) = delete;
    ~DiagnosticStream();

    DiagnosticStream& operator=(DiagnosticStream&& other) noexcept;
    DiagnosticStream& noquote() noexcept { return *this; }

    template <typename T>
    DiagnosticStream& operator<<(const T& value)
    {
        stream_ << value;
        return *this;
    }

private:
    DiagnosticLevel level_ = DiagnosticLevel::Info;
    std::string category_;
    std::ostringstream stream_;
    bool active_ = true;
};

DiagnosticStream diagnosticLog(DiagnosticLevel level, std::string_view category);
void setDiagnosticHandler(DiagnosticHandler handler);

enum class DiagnosticScope
{
    None,
    Session,
    View
};

std::uint64_t nextDiagnosticId();
std::string diagnosticOrigin(const Url& url);
} // namespace webview
