#pragma once

#include <system_webview/system_webview.h>

#include <string>
#include <vector>

namespace webview
{
struct ResourceMapping
{
    Url origin;
    std::string localDirectory;
    std::string entryDocument;
    bool spaFallback = false;
    ExternalNetworkAccess externalNetworkAccess = ExternalNetworkAccess::Denied;
};

Url normalizedOrigin(const Url& url);
bool validateResourceMappings(std::vector<ResourceMapping>* mappings, std::string* error);
const ResourceMapping* findResourceMapping(const std::vector<ResourceMapping>& mappings, const Url& url);
std::string resolveMappedResource(const ResourceMapping& mapping, const Url& url, std::string* error, bool allowSpaFallback = false);
std::string localBundleContentSecurityPolicy(ExternalNetworkAccess access);
} // namespace webview
