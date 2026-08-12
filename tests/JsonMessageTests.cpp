#include "webview/JsonMessage.h"

#include <cassert>

int main()
{
    QJsonObject input { { "type", "ping" }, { "sequence", 7 } };
    QJsonObject output;
    assert(webview::parseMessage(webview::jsonForJavaScriptArgument(input), &output));
    assert(output == input);
    assert(!webview::parseMessage(QStringLiteral("[]"), &output));
}
