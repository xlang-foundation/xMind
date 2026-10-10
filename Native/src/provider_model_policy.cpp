#include "agentflow/provider_model_policy.hpp"
#include <array>

namespace agentflow {
namespace {
bool capability(Capability value){return value==Capability::unknown||value==Capability::unsupported||value==Capability::supported;}
bool effort(ReasoningEffort value){return value==ReasoningEffort::none||value==ReasoningEffort::minimal||value==ReasoningEffort::low||value==ReasoningEffort::medium||value==ReasoningEffort::high||value==ReasoningEffort::xhigh||value==ReasoningEffort::max;}
}
ProviderModelPolicy documented_openai_model_policy(ProviderWire wire){
    if(wire!=ProviderWire::responses&&wire!=ProviderWire::chat_completions)
        throw std::invalid_argument("OpenAI model policy requires its declared native wire");
    // Official model pages and function-calling guide, consulted 2026-10-08.
    // https://developers.openai.com/api/docs/models/<page>
    // https://developers.openai.com/api/docs/guides/function-calling
    struct Fact {const char* id;bool reasoning,no_reasoning,maximum,chat_tools,chat_tools_none;};
    constexpr std::array facts{
        Fact{"gpt-6-astra",true,false,true,false,false},
        Fact{"gpt-6.1-sol",true,false,true,false,false},
        Fact{"gpt-6-sol",true,true,true,true,true},
        Fact{"gpt-6-luna",true,true,true,true,true},
        Fact{"gpt-5.6-sol",true,true,true,true,false},
        Fact{"gpt-5.6",true,true,true,true,false},
        Fact{"gpt-5.6-terra",true,true,true,true,false},
        Fact{"gpt-5.6-luna",true,true,true,true,false},
        Fact{"gpt-5.5",true,true,false,true,false},
        Fact{"gpt-5.5-2026-04-23",true,true,false,true,false},
        Fact{"gpt-5.4",true,true,false,true,false},
        Fact{"gpt-5.4-2026-03-05",true,true,false,true,false},
        Fact{"gpt-4.1",false,false,false,true,false},
        Fact{"gpt-4.1-2025-04-14",false,false,false,true,false},
        Fact{"gpt-4.1-mini",false,false,false,true,false},
        Fact{"gpt-4.1-mini-2025-04-14",false,false,false,true,false},
        Fact{"gpt-4.1-nano",false,false,false,true,false},
        Fact{"gpt-4.1-nano-2025-04-14",false,false,false,true,false},
        Fact{"gpt-4o",false,false,false,true,false},
        Fact{"gpt-4o-2024-08-06",false,false,false,true,false},
        Fact{"gpt-4o-2024-11-20",false,false,false,true,false}
    };
    ProviderModelPolicy result;result.wire=wire;
    for(const auto& fact:facts){
        NativeModelCapabilities value;
        value.tools=wire==ProviderWire::responses||fact.chat_tools?Capability::supported:Capability::unsupported;
        value.stream_usage=Capability::supported;value.output_limit=Capability::supported;
        if(fact.reasoning){
            value.reasoning_efforts={ReasoningEffort::low,ReasoningEffort::medium,ReasoningEffort::high,ReasoningEffort::xhigh};
            if(fact.no_reasoning)value.reasoning_efforts.insert(ReasoningEffort::none);
            if(fact.maximum)value.reasoning_efforts.insert(ReasoningEffort::max);
        }
        if(wire==ProviderWire::chat_completions&&fact.chat_tools_none)value.tool_reasoning_effort=ReasoningEffort::none;
        result.models.emplace(fact.id,std::move(value));
    }
    return result;
}
ProviderModelPolicy documented_xai_model_policy(){
    // xAI's Grok 4.7 documentation (consulted 2026-10-10) identifies Responses
    // as its primary API and documents custom function calling, streaming and
    // configurable reasoning. Catalogue entries alone cannot confer tools.
    // https://docs.x.ai/developers/grok-4-7
    // https://docs.x.ai/developers/tools/function-calling
    // https://docs.x.ai/developers/model-capabilities/text/streaming
    ProviderModelPolicy result;result.wire=ProviderWire::responses;
    NativeModelCapabilities grok;
    grok.tools=Capability::supported;grok.stream_usage=Capability::supported;grok.output_limit=Capability::supported;
    grok.reasoning_efforts={ReasoningEffort::low,ReasoningEffort::medium,ReasoningEffort::high,ReasoningEffort::xhigh};
    result.models.emplace("grok-4.7",std::move(grok));
    return result;
}
void validate_provider_model_policy(const ProviderModelPolicy& policy){
    if(policy.wire!=ProviderWire::chat_completions&&policy.wire!=ProviderWire::responses&&policy.wire!=ProviderWire::anthropic_messages&&policy.wire!=ProviderWire::gemini_generate_content)
        throw std::invalid_argument("Invalid native model policy wire");
    if(policy.models.size()>256)throw std::invalid_argument("Native model policy exceeds limits");
    for(const auto& [id,value]:policy.models){
        if(id.empty()||id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||
            !capability(value.tools)||!capability(value.stream_usage)||!capability(value.output_limit)||value.reasoning_efforts.size()>7)
            throw std::invalid_argument("Invalid native model capability declaration");
        for(const auto selected:value.reasoning_efforts)if(!effort(selected))throw std::invalid_argument("Invalid native model reasoning declaration");
        if(value.tool_reasoning_effort&&(value.tools!=Capability::supported||!value.reasoning_efforts.contains(*value.tool_reasoning_effort)))
            throw std::invalid_argument("Invalid native model tool reasoning requirement");
    }
}
ChatProviderConfig bind_provider_model_policy(const ProviderModelPolicy& policy,ChatProviderConfig provider,const std::string& model,bool require_tools){
    if(provider.wire!=policy.wire)throw std::invalid_argument("Native model policy wire differs from execution route");
    const auto found=policy.models.find(model);
    if(found==policy.models.end())throw std::invalid_argument("Model is not declared for native streamed text execution on this route");
    const auto& value=found->second;
    if(require_tools&&value.tools!=Capability::supported)throw std::invalid_argument("Model does not support native agent tools on this route; choose a tool-capable route and model");
    if(provider.reasoning_effort&&!value.reasoning_efforts.contains(*provider.reasoning_effort))throw std::invalid_argument("Model does not support the configured reasoning effort");
    if(require_tools&&value.tool_reasoning_effort){
        if(provider.reasoning_effort&&provider.reasoning_effort!=value.tool_reasoning_effort)
            throw std::invalid_argument("Configured reasoning effort is incompatible with model tools on this route");
        provider.reasoning_effort=value.tool_reasoning_effort;
    }
    provider.model=model;provider.tools=value.tools;provider.stream_usage=value.stream_usage;provider.output_limit=value.output_limit;
    provider.reasoning=value.reasoning_efforts.empty()?Capability::unsupported:Capability::supported;
    return provider;
}
}
