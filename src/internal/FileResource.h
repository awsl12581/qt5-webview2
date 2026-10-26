#pragma once

#include <system_webview/system_webview.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace webview
{
inline std::filesystem::path pathFromUtf8(std::string_view value)
{
#if defined(_WIN32)
    const auto text = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(text, text + value.size()));
#else
    return std::filesystem::path(std::string(value));
#endif
}

inline std::string pathToUtf8(const std::filesystem::path& value)
{
    const auto text = value.u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

class FileResource final : public ResourceBody
{
public:
    FileResource(std::string path, std::int64_t length)
        : path_(std::move(path))
        , length_(length)
        , remaining_(length)
        , file_(pathFromUtf8(path_), std::ios::binary)
    {
    }

    std::int64_t read(char* data, std::int64_t maximum) override
    {
        if (!file_ || !data || maximum <= 0 || remaining_ <= 0) {
            return 0;
        }
        const auto count = std::min(maximum, remaining_);
        file_.read(data, static_cast<std::streamsize>(count));
        const auto actual = static_cast<std::int64_t>(file_.gcount());
        remaining_ -= actual;
        return actual;
    }

    bool seek(std::int64_t offset) override
    {
        if (!file_ || offset < 0) {
            return false;
        }
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file_) {
            return false;
        }
        remaining_ = length_ - offset;
        return remaining_ >= 0;
    }

    std::string path() const override { return path_; }

private:
    std::string path_;
    std::int64_t length_ = 0;
    std::int64_t remaining_ = 0;
    std::ifstream file_;
};
} // namespace webview
