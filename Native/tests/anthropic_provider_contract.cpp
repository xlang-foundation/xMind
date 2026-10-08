#include "agentflow/model_provider.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
using namespace agentflow;
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    try{
        const std::string base=argv[1],token="claude-wire-fixture-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(token.data()),token.size()});
        ChatProviderConfig config{base+"/messages","fixture-claude",Capability::supported,Capability::supported,Capability::supported};config.wire=ProviderWire::anthropic_messages;
        ModelRequest request{{{MessageRole::system,"Native fixture instructions"},{MessageRole::user,"Read fixture"}},{{"read_file","Read a file",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"}},true,64};
        int done=0;const auto sink=[&](const ModelEvent& event){if(event.kind=="model.done")++done;};
        auto result=complete_model(config,request,&secret,sink);
        require(done==1&&result.finish_reason=="tool_calls"&&result.tool_calls.size()==1,"Claude adapter must acknowledge validated calls once");
        require(result.tool_calls[0].id=="toolu-fixture"&&result.tool_calls[0].name=="read_file"&&result.tool_calls[0].arguments_json==R"({"path":"README"})","Claude call identity/arguments changed");
        auto usage=nlohmann::json::parse(result.usage_json);require(usage["input_tokens"]==8&&usage["output_tokens"]==5&&!usage.contains("total_tokens"),"Claude adapter must preserve reported usage");
        request.messages.push_back({MessageRole::assistant,result.content,result.tool_calls});request.messages.push_back({MessageRole::tool,"actual fixture result",{},"toolu-fixture"});
        config.endpoint=base+"/continuation";done=0;result=complete_model(config,request,&secret,sink);
        require(done==1&&result.content=="Fixture complete"&&result.tool_calls.empty()&&result.finish_reason=="stop","Claude continuation must decode text");
        request.messages.resize(2);
        for(const auto* route:{"/unknown","/incomplete","/late-error"}){
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
        std::cout<<"Native Claude request/transport/stream adapter passed against an independent synthetic peer; no live inference or enrollment tested\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
