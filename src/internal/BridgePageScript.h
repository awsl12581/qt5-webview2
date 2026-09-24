#pragma once

#include <QString>

namespace webview
{
QString bridgePageScript(
    const QString& documentTokenExpression,
    const QString& nativePostExpression,
    const QString& nativeReceiveRegistration,
    const QString& prelude = { });
}
