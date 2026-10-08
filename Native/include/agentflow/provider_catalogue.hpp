#pragma once
#include "agentflow/http_stream_transport.hpp"
#include <vector>
namespace agentflow {
enum class ProviderCatalogueFormat {openai,anthropic};
struct ProviderCataloguePolicy {
    std::string endpoint;
    ProviderCatalogueFormat format=ProviderCatalogueFormat::openai;
};
// Read-only account discovery. Policy is native/backend-owned, not client input.
// Returns validated identities only; listing does not establish adapter support.
// No enrollment, persistence, redirects, retries or provider metadata forwarding.
std::vector<std::string> discover_provider_models(const ProviderCataloguePolicy& policy,
    const SecretBytes& key,std::stop_token cancel={});
}
