#pragma once

#include "internal/RandomToken.h"

#include <Cocoa/Cocoa.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace native_test
{
class TempDir final
{
public:
    TempDir()
    {
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt != 16; ++attempt) {
            path_ = root / ("system-webview-test-" + webview::randomToken128());
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
    }
    ~TempDir()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    bool isValid() const { return !path_.empty(); }
    std::string path() const { return path_.string(); }
    std::string filePath(std::string_view name) const { return (path_ / std::filesystem::path(name)).string(); }

private:
    std::filesystem::path path_;
};

inline bool writeFile(const std::string& path, std::string_view data)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    return output && static_cast<bool>(output.write(data.data(), static_cast<std::streamsize>(data.size())));
}

class RunLoop final
{
public:
    void quit() { stopped_.store(true); }
    void exec(std::chrono::milliseconds timeout = std::chrono::seconds(15))
    {
        stopped_.store(false);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!stopped_.load() && std::chrono::steady_clock::now() < deadline) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }

private:
    std::atomic<bool> stopped_ = false;
};

inline void runFor(std::chrono::milliseconds duration)
{
    RunLoop loop;
    std::thread([&loop, duration] {
        std::this_thread::sleep_for(duration);
        loop.quit();
    }).detach();
    loop.exec(duration + std::chrono::milliseconds(100));
}

class HttpServer final
{
public:
    using Handler = std::function<std::string(std::string_view, std::uint16_t)>;

    explicit HttpServer(Handler handler)
        : handler_(std::move(handler))
    {
        socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (socket_ >= 0 && ::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0
            && ::listen(socket_, 16) == 0) {
            socklen_t size = sizeof(address);
            ::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &size);
            port_ = ntohs(address.sin_port);
            thread_ = std::thread([this] { acceptLoop(); });
        }
    }

    ~HttpServer() { close(); }
    std::uint16_t port() const { return port_; }

    void close()
    {
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
            socket_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void acceptLoop()
    {
        while (socket_ >= 0) {
            const int client = ::accept(socket_, nullptr, nullptr);
            if (client < 0) {
                break;
            }
            std::string request;
            char buffer[4096];
            const auto count = ::recv(client, buffer, sizeof(buffer), 0);
            if (count > 0) {
                request.assign(buffer, static_cast<std::size_t>(count));
            }
            const auto response = handler_(request, port_);
            if (!response.empty()) {
                ::send(client, response.data(), response.size(), 0);
            }
            ::shutdown(client, SHUT_RDWR);
            ::close(client);
        }
    }

    Handler handler_;
    int socket_ = -1;
    std::uint16_t port_ = 0;
    std::thread thread_;
};
} // namespace native_test
