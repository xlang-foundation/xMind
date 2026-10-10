#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace agentflow {
// Native public-client registration settings. A zero port selects an available
// loopback port; an explicit port must bind exactly, without fallback.
struct McpOAuthLoopbackSetting {
    std::string path="/oauth/callback";
    std::uint16_t port=0;
};
inline void validate_mcp_oauth_loopback_path(std::string_view path) {
    if(path.empty()||path.size()>128||path.front()!='/')throw std::invalid_argument("Invalid native OAuth callback path");
    for(unsigned char c:path)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-'))throw std::invalid_argument("Invalid native OAuth callback path");
}
}
