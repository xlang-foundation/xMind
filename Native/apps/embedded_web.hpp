#pragma once
#include "../src/agent_worker_sdk_values.hpp"
#include "xlang3/cpp/runtime.h"
#include "httplib.h"
#include "python_host.hpp"
#include "../src/native_response_stream.hpp"
#include "../src/native_web_request_queue.hpp"
#include <functional>

namespace agentflow {
class BrowserResponse {
    std::shared_ptr<httplib::Response> response_;
    mutable std::mutex mutex_;
    std::shared_ptr<std::atomic_bool> cancelled_=std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<NativeResponseStream> stream_;
    std::function<bool()> running_;
    std::shared_ptr<NativeWebRequestState> pending_;
public:
    BrowserResponse()=default;
    BrowserResponse(std::shared_ptr<httplib::Response> response,std::shared_ptr<NativeWebRequestState> pending,std::function<bool()> running):response_(std::move(response)),cancelled_(pending->cancelled),running_(std::move(running)),pending_(std::move(pending)){}
    bool Ready()const{return !pending_||pending_->ready.load(std::memory_order_acquire);}
    std::int64_t Status()const{if(pending_)pending_->check();std::lock_guard lock(mutex_);if(!response_)throw std::runtime_error("Response closed");return response_->status;}
    std::string Body()const{if(pending_)pending_->check();std::lock_guard lock(mutex_);if(!response_)throw std::runtime_error("Response closed");return response_->body;}
    bool Streaming()const{if(pending_)pending_->check();std::lock_guard lock(mutex_);if(!response_)throw std::runtime_error("Response closed");return static_cast<bool>(response_->content_provider_);}
    X::Value Read(){
        if(pending_)pending_->check();
        std::shared_ptr<NativeResponseStream> current;
        {std::lock_guard lock(mutex_);if(cancelled_->load())throw std::runtime_error("Response closed");if(!stream_)stream_=std::make_shared<NativeResponseStream>(response_,cancelled_,running_);current=stream_;}
        const auto chunk=current->read();auto value=X::Value::Dict(Host());
        if(!value.Set("data",X::Value::Bytes(Host(),chunk.data.data(),chunk.data.size()))||!value.Set("done",X::Value(chunk.done)))throw std::runtime_error("Cannot construct stream observation");
        return value;
    }
    void Close(){cancelled_->store(true);std::shared_ptr<NativeResponseStream> current;{std::lock_guard lock(mutex_);current=stream_;}if(current)current->close();{std::lock_guard lock(mutex_);stream_.reset();response_.reset();}}
    BEGIN_PACKAGE(BrowserResponse)
        APISET().AddFunc<0>("Ready",&BrowserResponse::Ready);
        APISET().AddFunc<0>("Status",&BrowserResponse::Status);
        APISET().AddFunc<0>("Body",&BrowserResponse::Body);
        APISET().AddFunc<0>("Streaming",&BrowserResponse::Streaming);
        APISET().AddFunc<0>("Read",&BrowserResponse::Read);
        APISET().AddFunc<0>("Close",&BrowserResponse::Close);
    END_PACKAGE
};
class EmbeddedWeb {
    // The ASGI event loop has its own embedded runtime. The process's native
    // SDK listener remains free to serve worker/backend calls concurrently.
    X::Runtime runtime_;
    std::unique_ptr<X::Module> module_;
    X::Value host_;
    std::function<void(httplib::Request&,httplib::Response&)> dispatch_;
    std::string origin_,authorization_;
    int port_=0;
    std::function<bool()> running_;
    NativeWebRequestQueue requests_;
    static X::Value call(const X::Value& method,std::vector<X::Value> arguments={}){
        X::Value result;if(!method.Call(arguments,result))throw std::runtime_error("Embedded FastAPI call failed");return result;
    }
public:
    EmbeddedWeb(const std::string& package,const std::string& pure_packages,int preferred_port=0){
        configure_python_host(runtime_,package,{package+"/xmind.exe","web"},pure_packages);
        host_=runtime_.RegisterPackage("_xmind_browser",*this);
        module_=std::make_unique<X::Module>(runtime_,"xmind_web");
        port_=static_cast<int>(call((*module_)["prepare"],{X::Value(runtime_,preferred_port)}).ToInt64());
        if(port_<1||port_>65535)throw std::runtime_error("Invalid embedded browser port");
        origin_="http://127.0.0.1:"+std::to_string(port_);
    }
    int port()const{return port_;}
    void bind(std::function<void(httplib::Request&,httplib::Response&)> requests,std::string authorization,std::function<bool()> running={}){dispatch_=std::move(requests);authorization_=std::move(authorization);running_=std::move(running);}
    bool Running()const{return !running_||running_();}
    X::Value RequestAsync(std::string method,std::string target,X::Value fields,std::string body,bool owner){
        if(!dispatch_||target.size()>65536||body.size()>4*1024*1024||!target.starts_with("/v1/")||(method!="GET"&&method!="POST"&&method!="DELETE"))throw std::invalid_argument("Invalid native view request");
        httplib::Request request;request.method=std::move(method);request.target=std::move(target);request.body=std::move(body);
        const auto query=request.target.find('?');request.path=httplib::decode_uri_component(request.target.substr(0,query));if(query!=std::string::npos)httplib::detail::parse_query_text(request.target.substr(query+1),request.params);
        const auto headers=sdk_values::from_value(fields);if(!headers.is_object()||headers.size()>4)throw std::invalid_argument("Invalid native request headers");
        for(const auto& [name,value]:headers.items()){
            if(name!="Authorization"&&name!="X-XMind-View-Origin"&&name!="Content-Type"&&name!="Last-Event-ID")throw std::invalid_argument("Invalid native request header");
            request.headers.emplace(name,value.get<std::string>());
        }
        request.headers.emplace("Host","127.0.0.1:"+std::to_string(port_));
        if(owner){request.headers.erase("Authorization");request.headers.emplace("Authorization",authorization_);}
        auto response=std::make_shared<httplib::Response>();
        auto pending=requests_.start([request=std::move(request),response,dispatch=dispatch_](const auto& state)mutable{
            request.is_connection_closed=[cancelled=state->cancelled]{return cancelled->load();};dispatch(request,*response);
            if(response->status<0)response->status=200;
        });
        return __xlang3_package_->CreateInstance("Response",std::make_shared<BrowserResponse>(std::move(response),std::move(pending),running_));
    }
    void serve(const std::string& assets){
        try{call((*module_)["serve"],{host_,X::Value(runtime_,origin_),X::Value(runtime_,assets)});}catch(...){requests_.drain();throw;}
        requests_.drain();
    }
    BEGIN_PACKAGE(EmbeddedWeb)
        APISET().AddFunc<5>("RequestAsync",&EmbeddedWeb::RequestAsync);
        APISET().AddFunc<0>("Running",&EmbeddedWeb::Running);
        APISET().AddClass<0,BrowserResponse>("Response");
    END_PACKAGE
};
}
