#include "internal/RandomToken.h"

#if defined(_WIN32)
#include <windows.h>

#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#endif

namespace webview
{
std::string randomToken128()
{
    std::array<unsigned char, 16> bytes {};
    bool success = false;
#if defined(_WIN32)
    success = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    success = SecRandomCopyBytes(kSecRandomDefault, bytes.size(), bytes.data()) == errSecSuccess;
#else
    success = false;
#endif
    if (!success) {
        return {};
    }

    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}
} // namespace webview
