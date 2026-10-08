#include "agentflow/deepseek_model_policy.hpp"
namespace agentflow {
std::map<std::string,Capability> deepseek_documented_tool_policy(){
    // Official Chat Completions reference, consulted 2026-10-08:
    // https://api-docs.deepseek.com/api/create-chat-completion/
    return {{"deepseek-flash",Capability::supported},{"deepseek-v4-pro",Capability::supported}};
}
}
