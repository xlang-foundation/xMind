#include "agentflow/local_auth.hpp"
#include <stdexcept>
namespace agentflow {
void validate_local_auth_token(std::string_view token) {
    if(token.size()<32 || token.size()>256) throw std::invalid_argument("Authentication token must contain 32-256 printable bytes");
    for(unsigned char c:token) if(c<33 || c>126) throw std::invalid_argument("Authentication token must contain printable bytes without spaces");
}
}
