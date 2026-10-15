#include "agentflow/anthropic_history.hpp"
#include "agentflow/deepseek_provider.hpp"
#include "agentflow/gemini_history.hpp"
#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <stdexcept>

using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const IncompatibleProviderHistory&){return;}throw std::runtime_error("Expected incompatible provider history");}
ModelRequest conversation(ModelMessage assistant){ModelRequest request;request.messages={{MessageRole::user,"Synthetic prompt"},std::move(assistant)};return request;}
ModelMessage saved(std::string content,std::string receipt){ModelMessage result;result.role=MessageRole::assistant;result.content=std::move(content);result.provider_items_json=std::move(receipt);return result;}
}
int main(){try{
    ChatProviderConfig chat;chat.endpoint="https://example.invalid";chat.model="synthetic-chat";chat.wire=ProviderWire::chat_completions;chat.chat_dialect=ChatDialect::openai;
    const auto claude=anthropic_content_receipt(R"([{"type":"text","text":"Claude answer"}])","stop","end_turn");
    const auto gemini=Json::array({{{"type","gemini_content"},{"parts_json",R"([{"text":"Gemini answer"}])"},{"finish_reason","stop"},{"provider_finish_reason","STOP"},{"bindings",Json::array()}}}).dump();
    const auto responses=Json::array({{{"id","msg_synthetic"},{"type","message"},{"role","assistant"},{"content",Json::array({{{"type","output_text"},{"text","Responses answer"}}})}}}).dump();
    const auto deepseek=Json::array({{{"type","deepseek_assistant"},{"model","deepseek-chat"},{"message",{{"role","assistant"},{"content","DeepSeek answer"}}}}}).dump();
    for(const auto& [content,receipt]:std::vector<std::pair<std::string,std::string>>{{"Claude answer",claude},{"Gemini answer",gemini},{"DeepSeek answer",deepseek}}){
        auto request=conversation(saved(content,receipt));const auto original=request.messages.back().provider_items_json;const auto projected=project_provider_history(chat,request);
        require(projected.messages.back().provider_items_json=="[]","Foreign provider receipt must be removed from the request copy");
        require(request.messages.back().provider_items_json==original,"Projection must not mutate persisted conversation history");
        (void)serialize_chat_request(chat,projected);
    }
    ChatProviderConfig gemini_target=chat;gemini_target.wire=ProviderWire::gemini_generate_content;gemini_target.tools=Capability::supported;
    auto same=conversation(saved("Gemini answer",gemini));require(project_provider_history(gemini_target,same).messages.back().provider_items_json==gemini,"Same-wire replay must preserve its exact native receipt");
    auto corrupt=conversation(saved("Changed answer",claude));rejects([&]{(void)project_provider_history(chat,corrupt);});
    auto unknown=conversation(saved("Opaque answer",R"([{"type":"unknown_native_receipt"}])"));rejects([&]{(void)project_provider_history(chat,unknown);});
    auto foreign_responses=conversation(saved("Responses answer",responses));rejects([&]{(void)project_provider_history(chat,foreign_responses);});
    const auto response_reasoning=Json::array({{{"id","rs_synthetic"},{"type","reasoning"},{"summary",Json::array({{{"type","summary_text"},{"text","private provider reasoning"}}})}}}).dump();
    ChatProviderConfig responses_target=chat;responses_target.wire=ProviderWire::responses;
    auto same_responses=conversation(saved("Reasoned answer",response_reasoning));
    require(project_provider_history(responses_target,same_responses).messages.back().provider_items_json==response_reasoning,"Same-wire Responses reasoning must remain available for native replay");
    auto claude_to_responses=conversation(saved("Claude answer",claude));
    const auto bridged_claude=project_provider_history(responses_target,claude_to_responses);
    require(bridged_claude.messages.back().provider_items_json=="[]"&&bridged_claude.messages.back().content=="Claude answer",
        "Responses context projection must preserve portable canonical text and remove Claude-only receipts");
    auto foreign_reasoning=conversation(saved("Reasoned answer",response_reasoning));rejects([&]{(void)project_provider_history(chat,foreign_reasoning);});

    ModelMessage canonical;canonical.role=MessageRole::assistant;canonical.content="Calling tool";canonical.tool_calls={{"native-tool-id","read_file",R"({"path":"README.md"})"}};
    canonical.provider_items_json=anthropic_content_receipt(R"([{"type":"text","text":"Calling tool"},{"type":"tool_use","id":"native-tool-id","name":"read_file","input":{"path":"README.md"}}])","tool_calls","tool_use");
    auto with_tool=conversation(canonical);ModelMessage answer;answer.role=MessageRole::tool;answer.tool_call_id="native-tool-id";answer.content="file body";with_tool.messages.push_back(answer);
    const auto projected=project_provider_history(gemini_target,with_tool);require(projected.messages[1].provider_items_json=="[]","Foreign Claude tool receipt must be projected after validation");
    const auto responses_tool_history=project_provider_history(responses_target,with_tool);
    require(responses_tool_history.messages[1].provider_items_json=="[]"&&responses_tool_history.messages[1].tool_calls.size()==1&&
        responses_tool_history.messages[2].tool_call_id=="native-tool-id",
        "Responses context projection must retain canonical Claude tool history without its provider receipt");
    const auto adapted=gemini_model_request(projected,Capability::supported);const auto body=Json::parse(serialize_gemini_request(adapted));
    require(body["contents"][1]["parts"][1]["functionCall"]["name"]=="read_file"&&!body["contents"][1]["parts"][1]["functionCall"].contains("id"),"Cross-wire tool conversion must retain the canonical tool and omit provider-owned IDs");
    require(body["contents"][2]["parts"][0]["functionResponse"]["response"]["output"]=="file body","Cross-wire tool result must correlate and serialize");
    std::cout<<"Provider history bridge passed validated Claude, Gemini and DeepSeek conversion; Responses-only receipt retention; immutable history; same-wire replay; fail-closed unknown/corrupt receipts; and Claude-to-Gemini/Responses canonical tool conversion.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
