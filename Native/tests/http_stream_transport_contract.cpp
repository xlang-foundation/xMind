#include "agentflow/http_stream_transport.hpp"
#include "agentflow/model_stream.hpp"
#include <iostream>
#include <thread>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected transport rejection did not occur");
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        const std::string base=argv[1],tls=argv[2];
        const std::string synthetic="transport-test-token-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(synthetic.data()),synthetic.size()});
        auto request=[&](const std::string& path) {return HttpStreamRequest{base+path,R"({"fixture":"transport"})",10s,5s};};
        auto discovery=[&](const std::string& path){return HttpStreamRequest{base+path,"",10s,5s};};
        for(const auto mode:{CredentialHeader::x_api_key,CredentialHeader::x_goog_api_key}){
            const std::string name=mode==CredentialHeader::x_api_key?"api-key":"google-key";
            auto input=request("/auth/"+name);input.credential_header=mode;
            ChatCompletionStream decoder([](const ModelEvent&){});post_event_stream(input,&secret,[&](std::string_view bytes){decoder.feed(bytes);});require(decoder.finish().content=="transport fixture","API-key header must retain actual streaming bytes");
            auto json=discovery("/json-auth/"+name);json.credential_header=mode;require(get_json(json,&secret)==R"({"object":"list","data":[]})","API-key JSON discovery must use the selected header");
            input.url+="/redirect";rejects<ProviderHttpError>([&]{post_event_stream(input,&secret,[](std::string_view){});});
            rejects<std::invalid_argument>([&]{post_event_stream(input,nullptr,[](std::string_view){});});
            const std::string injected="transport-fixture\r\nx-extra: injected";SecretBytes invalid({reinterpret_cast<const std::uint8_t*>(injected.data()),injected.size()});
            rejects<std::invalid_argument>([&]{post_event_stream(input,&invalid,[](std::string_view){});});
        }
        {auto invalid=request("/ok");invalid.credential_header=static_cast<CredentialHeader>(999);rejects<std::invalid_argument>([&]{post_event_stream(invalid,&secret,[](std::string_view){});});}
        require(get_json(discovery("/json"),&secret)==R"({"object":"list","data":[]})","JSON GET must return actual native peer bytes");
        rejects<ProviderHttpError>([&]{get_json(discovery("/json-redirect"),&secret);});
        rejects<TransportError>([&]{get_json(discovery("/json-wrong-media"),&secret);});
        rejects<TransportError>([&]{get_json(discovery("/json-oversized"),&secret);});
        rejects<TransportError>([&]{get_json({tls+"/json","",10s,5s},nullptr);});
        rejects<std::invalid_argument>([&]{get_json({"http://example.invalid/models",""},&secret);});
        {
            std::stop_source cancellation;
            std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
            rejects<TransportCancelled>([&]{get_json(discovery("/json-delay"),&secret,cancellation.get_token());});
        }
        {auto timed=discovery("/json-delay");timed.deadline=200ms;rejects<TransportTimeout>([&]{get_json(timed,&secret);});}
        {
            ChatCompletionStream decoder([](const ModelEvent&){});
            post_event_stream(request("/ok"),&secret,[&](std::string_view bytes){decoder.feed(bytes);});
            require(decoder.finish().content=="transport fixture","Native socket bytes must reach decoder");
        }
        {
            bool error=false;
            try {post_event_stream(request("/error"),&secret,[](std::string_view){});}
            catch(const ProviderHttpError& exception) {
                require(exception.status==429,"Provider HTTP status must survive");
                require(std::string(exception.what()).find("do-not-log")==std::string::npos,"Provider error body must not be echoed");error=true;
            }
            require(error,"HTTP error must fail transport");
        }
        rejects<ProviderHttpError>([&]{post_event_stream(request("/redirect"),&secret,[](std::string_view){});});
        for(const auto* path:{"/diagnostic","/diagnostic-private","/diagnostic-large","/diagnostic-malformed","/diagnostic-stall"}) {
            bool rejected=false;auto input=request(path);input.deadline=500ms;
            try {post_event_stream(input,&secret,[](std::string_view){throw std::runtime_error("Error body reached consumer");});}
            catch(const ProviderHttpError& error) {
                rejected=true;require(error.status==400,"Diagnostic failures must preserve HTTP status");
                const auto detail=std::string(error.what())+error.type+error.code+error.param;
                require(detail.find("private-fixture-key")==std::string::npos && detail.find("do-not-log")==std::string::npos,"Diagnostics cannot expose private or message fields");
                if(std::string(path)=="/diagnostic" || std::string(path)=="/diagnostic-private") {
                    require(error.type=="invalid_request_error" && error.code=="unsupported_parameter","Known diagnostic identifiers must survive");
                    require(error.param==(std::string(path)=="/diagnostic"?"n":""),"Only allowlisted parameters may survive");
                } else require(error.type.empty()&&error.code.empty()&&error.param.empty(),"Incomplete diagnostics must be omitted");
            }
            require(rejected,"HTTP failure must remain an error");
        }
        {
            std::stop_source cancellation;
            std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
            rejects<TransportCancelled>([&]{post_event_stream(request("/diagnostic-stall"),&secret,[](std::string_view){},cancellation.get_token());});
        }
        rejects<TransportError>([&]{post_event_stream(request("/wrong-media"),&secret,[](std::string_view){});});
        rejects<TransportError>([&]{post_event_stream({tls+"/ok","{}",10s,5s},nullptr,[](std::string_view){});});
        rejects<std::invalid_argument>([&]{post_event_stream({"http://example.invalid/","{}"},&secret,[](std::string_view){});});
        rejects<std::invalid_argument>([&]{post_event_stream({"https://user:password@example.invalid/","{}"},&secret,[](std::string_view){});});
        {
            std::stop_source cancellation;cancellation.request_stop();
            rejects<TransportCancelled>([&]{post_event_stream(request("/ok"),&secret,[](std::string_view){},cancellation.get_token());});
        }
        {
            std::stop_source cancellation;
            std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
            const auto started=std::chrono::steady_clock::now();
            rejects<TransportCancelled>([&]{post_event_stream(request("/delay"),&secret,[](std::string_view){},cancellation.get_token());});
            require(std::chrono::steady_clock::now()-started<2s,"Cancellation must interrupt pending response");
        }
        {
            auto timed=request("/stall");timed.deadline=200ms;
            rejects<TransportTimeout>([&]{post_event_stream(timed,&secret,[](std::string_view){});});
        }
        {
            auto timed=request("/stall");timed.idle_timeout=200ms;
            rejects<TransportTimeout>([&]{post_event_stream(timed,&secret,[](std::string_view){});});
        }
        {
            bool stopped=false;
            try {post_event_stream(request("/ok"),&secret,[](std::string_view){throw std::logic_error("Consumer stopped");});}
            catch(const std::logic_error&) {stopped=true;}require(stopped,"Consumer exception must propagate");
        }
        // Fresh request after cancellation/exception checks cleanup and isolation.
        {ChatCompletionStream decoder([](const ModelEvent&){});post_event_stream(request("/ok"),&secret,[&](std::string_view bytes){decoder.feed(bytes);});require(decoder.finish().finish_reason=="stop","Transport must remain usable");}
        std::cout<<"Native transport contracts passed against synthetic protocol peer; no live model was called\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
