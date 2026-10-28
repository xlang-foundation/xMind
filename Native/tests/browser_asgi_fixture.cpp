// Test-only external-peer adapter. Production web requests use bound C++/SDK
// calls. This executable replaces the former Node HTTP fixture for ASGI tests.
#include "../apps/embedded_web.hpp"
#include <iostream>
#include <regex>
#include <future>

class PeerResponse {
public:
    std::shared_ptr<httplib::Response> response;
    std::shared_ptr<httplib::Client> client;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<agentflow::NativeResponseStream> stream;
    PeerResponse()=default;
    std::int64_t Status()const{return response->status;}
    std::string Body()const{return response->body;}
    bool Streaming()const{return static_cast<bool>(stream);}
    X::Value Read(){const auto chunk=stream->read();auto value=X::Value::Dict(Host());value.Set("data",X::Value::Bytes(Host(),chunk.data.data(),chunk.data.size()));value.Set("done",X::Value(chunk.done));return value;}
    void Close(){cancelled->store(true);if(client)client->stop();if(stream)stream->close();}
    ~PeerResponse(){Close();}
    BEGIN_PACKAGE(PeerResponse)
      APISET().AddFunc<0>("Status",&PeerResponse::Status);
      APISET().AddFunc<0>("Body",&PeerResponse::Body);
      APISET().AddFunc<0>("Streaming",&PeerResponse::Streaming);
      APISET().AddFunc<0>("Read",&PeerResponse::Read);
      APISET().AddFunc<0>("Close",&PeerResponse::Close);
    END_PACKAGE
};

class FixtureHost {
    std::string backend_,owner_;
public:
    explicit FixtureHost(const nlohmann::json& config):backend_(config.at("backend")),owner_(config.value("owner",std::string{})){
        if(!std::regex_match(backend_,std::regex(R"(http://127\.0\.0\.1:[1-9][0-9]{0,4})")))throw std::runtime_error("Fixture peers must be loopback HTTP");
    }
    bool Running()const{return true;}
    X::Value Request(std::string method,std::string target,X::Value fields,std::string body,bool owner){
        auto client=std::make_shared<httplib::Client>(backend_);client->set_path_encode(false);client->set_connection_timeout(2,0);client->set_read_timeout(120,0);client->set_write_timeout(2,0);
        httplib::Headers headers;const auto values=agentflow::sdk_values::from_value(fields);for(const auto& [name,value]:values.items())headers.emplace(name,value.get<std::string>());
        if(owner){headers.erase("Authorization");if(!owner_.empty())headers.emplace("Authorization","Bearer "+owner_);}
        auto response=std::make_shared<httplib::Response>();
        auto peer=std::make_shared<PeerResponse>();peer->response=response;peer->client=client;
        if(method=="GET"&&target.substr(0,target.find('?')).ends_with("/stream")){
            auto ready=std::make_shared<std::promise<void>>();auto metadata=ready->get_future();auto opened=std::make_shared<std::atomic_bool>(false);
            auto producer=std::make_shared<httplib::Response>();
            producer->set_chunked_content_provider("text/event-stream",[client,target,headers,ready,opened,response](std::size_t,httplib::DataSink& sink){
                const auto result=client->Get(target,headers,[&](const httplib::Response& value){response->status=value.status;response->headers=value.headers;opened->store(true);ready->set_value();return true;},[&](const char* data,std::size_t size){return sink.is_writable()&&sink.write(data,size);});
                if(!opened->exchange(true))ready->set_exception(std::make_exception_ptr(std::runtime_error("Fixture peer stream unavailable")));
                if(!result)return false;sink.done();return true;
            });
            peer->stream=std::make_shared<agentflow::NativeResponseStream>(producer,peer->cancelled);
            if(metadata.wait_for(std::chrono::seconds(15))!=std::future_status::ready){peer->Close();throw std::runtime_error("Fixture stream metadata unavailable");}metadata.get();
        }else{
            httplib::Request request;request.method=method;request.path=target;request.headers=std::move(headers);request.body=std::move(body);
            const auto result=client->send(request);if(!result)throw std::runtime_error("Fixture peer unavailable");*response=*result;
        }
        if(peer->stream&&response->status!=200){
            for(;;){const auto chunk=peer->stream->read();response->body+=chunk.data;if(response->body.size()>16*1024*1024)throw std::runtime_error("Fixture peer response exceeds limits");if(chunk.done)break;}
            peer->Close();peer->stream.reset();
        }
        return __xlang3_package_->CreateInstance("Response",peer);
    }
    BEGIN_PACKAGE(FixtureHost)
      APISET().AddFunc<5>("Request",&FixtureHost::Request);
      APISET().AddFunc<0>("Running",&FixtureHost::Running);
      APISET().AddClass<0,PeerResponse>("Response");
    END_PACKAGE
};
int main(){
    try{
        std::string input;std::getline(std::cin,input);if(input.size()>65536)throw std::runtime_error("Fixture configuration exceeds limits");const auto config=nlohmann::json::parse(input);
        FixtureHost fixture(config);X::Runtime runtime;auto paths=runtime.List();
        for(const auto* key:{"packages","web_source","modules","stdlib"}){const auto path=config.at(key).get<std::string>();runtime.AddImportRoot(path);paths.Append(X::Value(runtime,path));}
        X::Module system(runtime,"sys");system.SetAttr("path",paths);system.SetAttr("dont_write_bytecode",X::Value(runtime,true));
        const auto host=runtime.RegisterPackage("_xmind_browser_fixture",fixture);X::Module web(runtime,"xmind_web");
        X::Value port;if(!web["prepare"].Call(std::vector<X::Value>{X::Value(runtime,config.value("port",0))},port))throw std::runtime_error(runtime.LastError());
        const auto origin="http://127.0.0.1:"+std::to_string(port.ToInt64());
        std::cout<<nlohmann::json{{"origin",origin}}.dump()<<std::endl;
        X::Value result;if(!web["serve"].Call(std::vector<X::Value>{host,X::Value(runtime,origin),X::Value(runtime,config.at("assets").get<std::string>())},result))throw std::runtime_error(runtime.LastError());
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
