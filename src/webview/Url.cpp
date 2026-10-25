#include <system_webview/system_webview.h>

#include <ada.h>

#include <algorithm>
#include <charconv>
#include <string>

namespace
{
std::string decodePath(std::string_view path)
{
    std::string decoded;
    decoded.reserve(path.size());
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == '%' && index + 2 < path.size()) {
            const auto digit = [](char value) -> int {
                if (value >= '0' && value <= '9') {
                    return value - '0';
                }
                if (value >= 'a' && value <= 'f') {
                    return value - 'a' + 10;
                }
                if (value >= 'A' && value <= 'F') {
                    return value - 'A' + 10;
                }
                return -1;
            };
            const int high = digit(path[index + 1]);
            const int low = digit(path[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(path[index]);
    }
    return decoded;
}

std::string encodeRoute(std::string_view route)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(route.size());
    for (const unsigned char value : route) {
        if (value == '%' || value == '?' || value == '#' || value == '\\') {
            encoded.push_back('%');
            encoded.push_back(hex[value >> 4]);
            encoded.push_back(hex[value & 0x0f]);
        }
        else {
            encoded.push_back(static_cast<char>(value));
        }
    }
    return encoded;
}
}

namespace webview
{
Url::Url(std::string_view absoluteUrl)
{
    auto parsed = ada::parse<ada::url_aggregator>(absoluteUrl);
    if (parsed) {
        href_ = parsed->get_href();
    }
}

Url Url::fromLocalFile(std::string_view path)
{
    return Url(ada::href_from_file(path));
}

bool Url::isValid() const noexcept
{
    return !href_.empty();
}

bool Url::isEmpty() const noexcept
{
    return href_.empty();
}

bool Url::hasQuery() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    return parsed && parsed->has_search();
}

bool Url::hasFragment() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    return parsed && parsed->has_hash();
}

bool Url::hasCredentials() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    return parsed && parsed->has_credentials();
}

std::string Url::scheme() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    if (!parsed) {
        return {};
    }
    auto protocol = parsed->get_protocol();
    if (!protocol.empty() && protocol.back() == ':') {
        protocol.remove_suffix(1);
    }
    return std::string(protocol);
}

std::string Url::host() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    return parsed ? std::string(parsed->get_hostname()) : std::string();
}

int Url::port() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    if (!parsed || parsed->get_port().empty()) {
        return -1;
    }
    int value = -1;
    const auto port = parsed->get_port();
    const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), value);
    return error == std::errc() && end == port.data() + port.size() ? value : -1;
}

std::string Url::path() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    return parsed ? std::string(parsed->get_pathname()) : std::string();
}

std::string Url::fileName() const
{
    const auto value = decodePath(path());
    const auto slash = value.find_last_of('/');
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

std::string Url::toString() const
{
    return href_;
}

std::string Url::toLocalFile() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    if (!parsed || parsed->get_protocol() != "file:") {
        return {};
    }
    auto localPath = decodePath(parsed->get_pathname());
    const auto hostname = parsed->get_hostname();
    if (!hostname.empty()) {
        return "//" + std::string(hostname) + localPath;
    }
#if defined(_WIN32)
    if (localPath.size() >= 3 && localPath[0] == '/' && localPath[2] == ':') {
        localPath.erase(0, 1);
    }
    std::replace(localPath.begin(), localPath.end(), '/', '\\');
#endif
    return localPath;
}

Url Url::origin() const
{
    const auto parsed = ada::parse<ada::url_aggregator>(href_);
    if (!parsed) {
        return {};
    }
    if (parsed->get_protocol() == "app:") {
        return Url("app://" + std::string(parsed->get_hostname()) + "/");
    }
    const auto value = parsed->get_origin();
    return value == "null" ? Url() : Url(value);
}

Url Url::resolved(std::string_view reference) const
{
    auto base = ada::parse<ada::url_aggregator>(href_);
    if (!base) {
        return {};
    }
    auto resolved = ada::parse<ada::url_aggregator>(reference, &*base);
    return resolved ? Url(resolved->get_href()) : Url();
}

Url Url::withPath(std::string_view value) const
{
    const auto base = origin();
    if (!base.isValid()) {
        return {};
    }
    const auto route = encodeRoute(value);
    return base.resolved(route);
}
} // namespace webview
