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
