#pragma once

namespace webview::httpStatus
{
constexpr int kOk = 200;
constexpr int kPartialContent = 206;
constexpr int kForbidden = 403;
constexpr int kNotFound = 404;
constexpr int kGone = 410;
constexpr int kRangeNotSatisfiable = 416;
constexpr int kInternalServerError = 500;
} // namespace webview::httpStatus
