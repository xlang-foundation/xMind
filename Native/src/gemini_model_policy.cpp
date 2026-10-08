#include "agentflow/gemini_model_policy.hpp"
#include <array>

namespace agentflow {
std::map<std::string,Capability> gemini_documented_tool_policy(){
    constexpr std::array ids={
        "gemini-3.8-flash","gemini-3.7-flash","gemini-3.6-flash",
        "gemini-3.5-flash","gemini-3.5-flash-lite",
        "gemini-3.1-pro-preview","gemini-3.1-pro-preview-customtools",
        "gemini-3-flash-preview","gemini-2.5-pro",
        "gemini-2.5-flash","gemini-2.5-flash-lite"
    };
    std::map<std::string,Capability> result;
    for(const auto* id:ids)result.emplace(std::string("models/")+id,Capability::supported);
    return result;
}
}
