#pragma once
#include "agentflow/local_profile.hpp"
#include "agent_worker_sdk_values.hpp"
#include "xlang3/cpp/runtime.h"
#include "httplib.h"
#include "native_response_stream.hpp"
#include <cstring>

namespace agentflow {
// In-process calls stay in-process. Other xMind owners use the SDK's captured
// remote object, after checking its advertised endpoint against the owner PID.
class NativeRequestChannel {
    struct Reply {
        std::unique_ptr<X::Runtime> runtime;
        X::Value object;
        X::Value job;
        std::shared_ptr<httplib::Response> response=std::make_shared<httplib::Response>();
        std::string pending;
        std::size_t offset=0;
        bool done=false;
        std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
        std::unique_ptr<NativeResponseStream> local_stream;
        static X::Value call(const X::Value& method,const std::vector<X::Value>& args={}){
            X::Value result;if(!method.Call(args,result))throw std::runtime_error("Native SDK observation unavailable; no replay authorized");return result;
        }
        ~Reply(){cancelled->store(true);if(local_stream)local_stream->close();if(object.IsValid())try{call(object["Close"]);}catch(...){}if(job.IsValid())try{call(job["Close"]);}catch(...){} }
        std::string next(){
            if(object.IsValid()){
                const auto value=call(object["Read"]);done=value["done"].ToInt64()!=0;const auto data=value["data"];
                // Preserve early development peers until their controlled
                // upgrade; new peers carry raw SDK bytes across chunk edges.
                if(data.IsString())return data.ToString();
                std::uint64_t size=0;const auto* bytes=static_cast<const char*>(data.BytesData(&size));
                if(size>16384||(size&&!bytes))throw std::runtime_error("Invalid native event bytes");
                return size?std::string(bytes,static_cast<std::size_t>(size)):std::string{};
            }
            if(!local_stream)local_stream=std::make_unique<NativeResponseStream>(response,cancelled);
            const auto chunk=local_stream->read();done=chunk.done;return chunk.data;
        }
    };
    const LocalProfileConnection& connection_;
    std::shared_ptr<Reply> request(const std::string& method,const std::string& target,const httplib::Headers& headers,const std::string& body){
        auto result=std::make_shared<Reply>();
        if(connection_.bound_requests){
            httplib::Request request;request.method=method;request.target=target;request.body=body;request.headers=headers;
            request.is_connection_closed=[cancellation=result->cancelled]{return cancellation->load();};
            request.headers.emplace("Host","127.0.0.1:"+std::to_string(connection_.port));
            const auto query=target.find('?');request.path=httplib::decode_uri_component(target.substr(0,query));if(query!=std::string::npos)httplib::detail::parse_query_text(target.substr(query+1),request.params);
            connection_.bound_requests(request,*result->response);return result;
        }
        if(connection_.ipc_endpoint.empty())throw std::runtime_error("This preserved backend does not advertise xlang3 IPC for browser requests; use a qualified backend");
        result->runtime=std::make_unique<X::Runtime>();auto& runtime=*result->runtime;
        X3LrpcEndpointInfo info{};info.size=sizeof(info);
        if(x3_runtime_probe_remote(runtime.get(),connection_.ipc_endpoint.c_str(),1000,&info)!=X3_STATUS_OK||info.pid!=connection_.process_id)throw std::runtime_error("Native backend IPC owner identity differs");
        const auto root=runtime.ImportRemote("xmind",connection_.ipc_endpoint.c_str());
        const std::string owner(reinterpret_cast<const char*>(connection_.auth.view().data()),connection_.auth.view().size());
        auto service=Reply::call(root["WebRequests"],{X::Value(runtime,owner)});
        nlohmann::json fields=nlohmann::json::object();for(const auto& [name,value]:headers)fields[name]=value;
        const auto input=sdk_values::to_value(runtime.host(),nlohmann::json{{"method",method},{"target",target},{"headers",fields},{"body",body}});
        // Capability discovery is read-only. An admitted request is sent once;
        // a failed snapshot call never falls back to another invocation.
        X::Value protocol;const auto capability=service["ReplyProtocol"];
        const auto version=capability.IsValid()&&capability.Call(std::vector<X::Value>{},protocol)?protocol.ToInt64():0;
        if(version==3||version==2){
            X::Value snapshot;
            if(version==3){
                result->job=Reply::call(service["BeginRequest"],{input});
                // Preserve the managed control channel's existing 60-second
                // observation budget without blocking one SDK call that long.
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while(!Reply::call(result->job["Ready"]).ToInt64()){
                    if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Native SDK observation deadline elapsed; admitted request was not replayed");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                snapshot=Reply::call(result->job["Snapshot"]);
            }else snapshot=Reply::call(service["RequestSnapshot"],{input});
            result->response->status=static_cast<int>(snapshot["status"].ToInt64());
            const auto values=sdk_values::from_value(snapshot["headers"]);for(const auto& [name,value]:values.items())result->response->headers.emplace(name,value.get<std::string>());
            result->response->body=snapshot["body"].ToString();if(snapshot["streaming"].ToInt64())result->object=snapshot["response"];return result;
        }
        result->object=Reply::call(service["Request"],{input});
        result->response->status=static_cast<int>(Reply::call(result->object["Status"]).ToInt64());
        const auto values=sdk_values::from_value(Reply::call(result->object["Headers"]));for(const auto& [name,value]:values.items())result->response->headers.emplace(name,value.get<std::string>());
        result->response->body=Reply::call(result->object["Body"]).ToString();return result;
    }
public:
    explicit NativeRequestChannel(const LocalProfileConnection& connection):connection_(connection){}
    std::shared_ptr<httplib::Response> Get(const std::string& target,const httplib::Headers& headers){return request("GET",target,headers,"")->response;}
    std::shared_ptr<httplib::Response> Post(const std::string& target,const httplib::Headers& headers,const std::string& body,const std::string& type){auto fields=headers;fields.emplace("Content-Type",type);return request("POST",target,fields,body)->response;}
    std::shared_ptr<httplib::Response> Delete(const std::string& target,const httplib::Headers& headers,const std::string& body,const std::string& type){auto fields=headers;fields.emplace("Content-Type",type);return request("DELETE",target,fields,body)->response;}
    struct StreamHandle {
        std::shared_ptr<Reply> reply;
        std::shared_ptr<httplib::Response> response;
        bool is_valid()const{return static_cast<bool>(reply);}
        bool has_read_error()const{return false;}
        std::ptrdiff_t read(char* buffer,std::size_t count,const std::function<bool()>& writable={}){
            while(reply->pending.empty()&&!reply->done){if(writable&&!writable())return 0;reply->pending=reply->next();if(reply->pending.empty()&&!reply->done)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            const auto size=std::min(count,reply->pending.size());std::memcpy(buffer,reply->pending.data(),size);reply->pending.erase(0,size);return static_cast<std::ptrdiff_t>(size);
        }
    };
    StreamHandle open_stream(const std::string& method,const std::string& target,const std::string& body,const httplib::Headers& headers){auto value=request(method,target,headers,body);return {value,value->response};}
};
}
