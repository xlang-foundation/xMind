#include "agentflow/model_provider.hpp"
#include <iostream>

using namespace agentflow;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    try {
        const std::string base=argv[1],token="provider-protocol-test-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(token.data()),token.size()});
        ChatProviderConfig config{base+"/chat","fixture-deployment",Capability::supported,Capability::supported,Capability::supported};
        ModelRequest request{{{MessageRole::user,"Protocol fixture"}},{{"read_file","Read a file",R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})"}},true,64};
        bool done=false;
        const auto result=complete_chat(config,request,&secret,[&](const ModelEvent& event){if(event.kind=="model.done") done=true;});
        require(done && result.tool_calls.size()==1 && result.tool_calls[0].name=="read_file","Native adapter must deliver validated tool response");
        require(result.tool_calls[0].arguments_json==R"({"path":"README"})","Tool arguments must remain intact");
        for(const auto* route:{"/unknown","/incomplete"}) {
            config.endpoint=base+route;done=false;bool rejected=false;
            try {complete_chat(config,request,&secret,[&](const ModelEvent& event){if(event.kind=="model.done") done=true;});}
            catch(const ModelProtocolError&) {rejected=true;}
            require(rejected && !done,"Invalid/unoffered tool response must not publish completion");
        }
        config.endpoint=base+"/bad-feature";config.tools=Capability::unknown;
        bool rejected=false;
        try {complete_chat(config,request,&secret,[](const ModelEvent&){});}
        catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"Unknown requested capability must fail before network request");
        std::cout<<"Native chat adapter wire contracts passed against a synthetic peer; no live model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
