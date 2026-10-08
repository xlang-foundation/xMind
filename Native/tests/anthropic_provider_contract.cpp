#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <vector>
using namespace agentflow;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void actual_usage(const ModelCompletion& result,const std::vector<Json>& events,Json expected){
    require(Json::parse(result.usage_json)==expected,"Claude completion must normalize only supplied uncached input/output/cache counts without a total");
    require(events.size()==2&&events.back()==expected,"Claude usage events and completion must carry identical normalized final counters");
    expected["output_tokens"]=0;expected["completion_tokens"]=0;
    require(events.front()==expected,"Claude initial usage must preserve supplied output zero and cache presence");
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        const std::string base=argv[1],token="claude-wire-fixture-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(token.data()),token.size()});
        ChatProviderConfig config{base+"/messages","fixture-claude",Capability::supported,Capability::supported,Capability::supported};config.wire=ProviderWire::anthropic_messages;
        ModelRequest request{{{MessageRole::system,"Native fixture instructions"},{MessageRole::user,"Read fixture"}},{{"read_file","Read a file",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"}},true,64};
        int done=0;std::vector<Json> usage_events;const auto sink=[&](const ModelEvent& event){if(event.kind=="model.done")++done;if(event.kind=="model.usage")usage_events.push_back(Json::parse(event.json));};
        auto result=complete_model(config,request,&secret,sink);
        require(done==1&&result.finish_reason=="tool_calls"&&result.tool_calls.size()==1,"Claude adapter must acknowledge validated calls once");
        require(result.tool_calls[0].id=="toolu-fixture"&&result.tool_calls[0].name=="read_file"&&result.tool_calls[0].arguments_json==R"({"path":"README"})","Claude call identity/arguments changed");
        actual_usage(result,usage_events,{{"input_tokens",8},{"output_tokens",5},{"cache_creation_input_tokens",3},{"cache_read_input_tokens",7},{"prompt_tokens",8},{"completion_tokens",5},{"input_tokens_scope","uncached"},{"prompt_tokens_details",{{"cached_tokens",7}}}});
        request.messages.push_back({MessageRole::assistant,result.content,result.tool_calls});request.messages.push_back({MessageRole::tool,"actual fixture result",{},"toolu-fixture"});
        config.endpoint=base+"/continuation";done=0;usage_events.clear();result=complete_model(config,request,&secret,sink);
        require(done==1&&result.content=="Fixture complete"&&result.tool_calls.empty()&&result.finish_reason=="stop","Claude continuation must decode text");
        actual_usage(result,usage_events,{{"input_tokens",8},{"output_tokens",5},{"prompt_tokens",8},{"completion_tokens",5},{"input_tokens_scope","uncached"}});
        request.messages.resize(2);
        config.endpoint=base+"/zero-cache";done=0;usage_events.clear();result=complete_model(config,request,&secret,sink);
        require(done==1&&result.content=="Fixture complete"&&result.finish_reason=="stop","Claude zero-counter text completion must remain valid");
        actual_usage(result,usage_events,{{"input_tokens",0},{"output_tokens",0},{"cache_creation_input_tokens",0},{"cache_read_input_tokens",0},{"prompt_tokens",0},{"completion_tokens",0},{"input_tokens_scope","uncached"},{"prompt_tokens_details",{{"cached_tokens",0}}}});
        for(const auto* route:{"/unknown","/incomplete","/late-error","/bad-usage"}){
            config.endpoint=base+route;done=0;bool rejected=false;try{complete_model(config,request,&secret,sink);}catch(const ModelProtocolError&){rejected=true;}
            require(rejected&&done==0,"Invalid/unoffered Claude completion must remain unacknowledged");
        }
        for(const auto* route:{"/unauthorized","/redirect"}){
            config.endpoint=base+route;done=0;bool rejected=false;try{complete_model(config,request,&secret,sink);}catch(const ProviderHttpError& error){rejected=error.status==(std::string(route)=="/redirect"?307:401);}
            require(rejected&&done==0,"Claude HTTP failure must retain status without completion");
        }
        config.endpoint=base+"/must-not-arrive";
        for(int mode=0;mode<3;++mode){
            auto selected=config;auto invalid=request;if(mode==0)selected.tools=Capability::unknown;if(mode==1)invalid.max_output_tokens.reset();
            bool rejected=false;try{complete_model(selected,invalid,mode==2?nullptr:&secret,sink);}catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"Claude invalid capability, output limit or credential must fail before wire");
        }
        std::cout<<"Native Claude request/transport/stream adapter passed supplied uncached input/output/cache normalization, event/completion consistency, zero/missing cache preservation and malformed usage rejection against an independent synthetic peer; no live inference or enrollment tested\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
