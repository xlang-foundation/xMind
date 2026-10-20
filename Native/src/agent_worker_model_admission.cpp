#include "agentflow/agent_worker_model_admission.hpp"
#include "nlohmann/json.hpp"
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(){throw ModelProtocolError("Invalid worker model admission");}
const char* cap(Capability v){switch(v){case Capability::unknown:return "unknown";case Capability::unsupported:return "unsupported";case Capability::supported:return "supported";}invalid();}
Capability cap(const std::string& v){if(v=="unknown")return Capability::unknown;if(v=="unsupported")return Capability::unsupported;if(v=="supported")return Capability::supported;invalid();}
const char* wire(ProviderWire v){switch(v){case ProviderWire::chat_completions:return "chat-completions";case ProviderWire::responses:return "responses";case ProviderWire::anthropic_messages:return "anthropic-messages";case ProviderWire::gemini_generate_content:return "gemini-generate-content";}invalid();}
ProviderWire wire(const std::string& v){if(v=="chat-completions")return ProviderWire::chat_completions;if(v=="responses")return ProviderWire::responses;if(v=="anthropic-messages")return ProviderWire::anthropic_messages;if(v=="gemini-generate-content")return ProviderWire::gemini_generate_content;invalid();}
const char* dialect(ChatDialect v){switch(v){case ChatDialect::openai:return "openai";case ChatDialect::deepseek:return "deepseek";case ChatDialect::mistral:return "mistral";}invalid();}
ChatDialect dialect(const std::string& v){if(v=="openai")return ChatDialect::openai;if(v=="deepseek")return ChatDialect::deepseek;if(v=="mistral")return ChatDialect::mistral;invalid();}
const char* effort(ReasoningEffort v){switch(v){case ReasoningEffort::none:return "none";case ReasoningEffort::minimal:return "minimal";case ReasoningEffort::low:return "low";case ReasoningEffort::medium:return "medium";case ReasoningEffort::high:return "high";case ReasoningEffort::xhigh:return "xhigh";case ReasoningEffort::max:return "max";}invalid();}
ReasoningEffort effort(const std::string& v){if(v=="none")return ReasoningEffort::none;if(v=="minimal")return ReasoningEffort::minimal;if(v=="low")return ReasoningEffort::low;if(v=="medium")return ReasoningEffort::medium;if(v=="high")return ReasoningEffort::high;if(v=="xhigh")return ReasoningEffort::xhigh;if(v=="max")return ReasoningEffort::max;invalid();}
void validate(const AgentWorkerModelAdmission& v){
    if(v.model_id.empty()||v.model_id.size()>1024||v.model_id.find_first_of("\r\n")!=std::string::npos||v.model_id.find('\0')!=std::string::npos||
       (v.max_output_tokens&&(*v.max_output_tokens<=0||*v.max_output_tokens>9007199254740991LL)))invalid();
    (void)wire(v.wire);(void)cap(v.tools);(void)cap(v.stream_usage);(void)cap(v.output_limit);(void)cap(v.reasoning);(void)dialect(v.chat_dialect);if(v.reasoning_effort)(void)effort(*v.reasoning_effort);
    if(v.wire!=ProviderWire::chat_completions&&v.chat_dialect!=ChatDialect::openai)invalid();
}
}
std::string encode_agent_worker_model_admission(const AgentWorkerModelAdmission& v){validate(v);return Json{{"version",1},{"model_id",v.model_id},{"wire",wire(v.wire)},{"tools",cap(v.tools)},{"stream_usage",cap(v.stream_usage)},{"output_limit",cap(v.output_limit)},{"reasoning",cap(v.reasoning)},{"reasoning_effort",v.reasoning_effort?Json(effort(*v.reasoning_effort)):Json(nullptr)},{"chat_dialect",dialect(v.chat_dialect)},{"include_usage",v.include_usage},{"max_output_tokens",v.max_output_tokens?Json(*v.max_output_tokens):Json(nullptr)},{"owned_request_window",v.owned_request_window}}.dump();}
AgentWorkerModelAdmission decode_agent_worker_model_admission(const std::string& bytes){
    try{if(bytes.empty()||bytes.size()>16384)invalid();std::vector<std::set<std::string>> keys;const auto value=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& v){if(depth>3)invalid();if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key&&(keys.empty()||!keys.back().insert(v.get<std::string>()).second))invalid();if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});
        if(!value.is_object()||value.size()!=12||!value.at("version").is_number_integer()||value.at("version")!=1||!value.at("include_usage").is_boolean()||!value.at("owned_request_window").is_boolean())invalid();
        AgentWorkerModelAdmission result;result.model_id=value.at("model_id").get<std::string>();result.wire=wire(value.at("wire").get<std::string>());result.tools=cap(value.at("tools").get<std::string>());result.stream_usage=cap(value.at("stream_usage").get<std::string>());result.output_limit=cap(value.at("output_limit").get<std::string>());result.reasoning=cap(value.at("reasoning").get<std::string>());result.chat_dialect=dialect(value.at("chat_dialect").get<std::string>());result.include_usage=value.at("include_usage").get<bool>();result.owned_request_window=value.at("owned_request_window").get<bool>();
        if(!value.at("reasoning_effort").is_null())result.reasoning_effort=effort(value.at("reasoning_effort").get<std::string>());
        if(!value.at("max_output_tokens").is_null()){if(!value.at("max_output_tokens").is_number_integer())invalid();result.max_output_tokens=value.at("max_output_tokens").get<std::int64_t>();}validate(result);return result;
    }catch(const Json::exception&){invalid();}
}
ChatProviderConfig agent_worker_model_config(const AgentWorkerModelAdmission& v){
    validate(v);if((v.include_usage&&v.stream_usage!=Capability::supported)||(v.max_output_tokens&&v.output_limit!=Capability::supported)||(v.reasoning_effort&&v.reasoning!=Capability::supported))throw std::invalid_argument("Worker model policy lacks captured control support");
    ChatProviderConfig result;result.endpoint="worker://captured";result.model=v.model_id;result.wire=v.wire;result.tools=v.tools;result.stream_usage=v.include_usage?Capability::supported:Capability::unsupported;result.output_limit=v.output_limit;result.reasoning=v.reasoning;result.reasoning_effort=v.reasoning_effort;result.chat_dialect=v.chat_dialect;return result;
}
}
