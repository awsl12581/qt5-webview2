#pragma once

#include <array>
#include <string>

namespace webview
{
// Returns 128 bits from the platform cryptographic random source as lowercase hex.
std::string randomToken128();
} // namespace webview
