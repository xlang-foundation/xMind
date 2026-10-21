#pragma once
#include "agentflow/agent_worker_model_host.hpp"
#include "xlang3/xlang3.h"
#include <memory>
namespace agentflow {
struct NativeWorkerModelState;
struct NativeWorkerModelAccess {std::string endpoint,ticket;std::uint32_t pid=0;};
class NativeWorkerModelBinding final {
public:
    explicit NativeWorkerModelBinding(AgentWorkerModelHost&);
    ~NativeWorkerModelBinding();
    NativeWorkerModelBinding(const NativeWorkerModelBinding&)=delete;
    NativeWorkerModelBinding& operator=(const NativeWorkerModelBinding&)=delete;
    NativeWorkerModelAccess access()const;
private:std::shared_ptr<NativeWorkerModelState> state_;std::string ticket_;
};
struct NativeWorkerModelApi;
struct NativeWorkerApi;
struct NativeWorkersApi;
struct NativeWorkerModelApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerModelApi()=default;
    explicit NativeWorkerModelApi(NativeWorkerApi* worker);
    X::Value Admission();
    X::Value NewRequest();
    X::Value Prepare();
    X::Value Rebuild(std::string attempt,std::uint64_t request);
    X::Value Complete(std::string attempt,std::uint64_t request);
    BEGIN_PACKAGE(NativeWorkerModelApi)
        APISET().AddFunc<0>("Admission",&NativeWorkerModelApi::Admission);
        APISET().AddFunc<0>("NewRequest",&NativeWorkerModelApi::NewRequest);
        APISET().AddFunc<0>("Prepare",&NativeWorkerModelApi::Prepare);
        APISET().AddFunc<2>("Rebuild",&NativeWorkerModelApi::Rebuild);
        APISET().AddFunc<2>("Complete",&NativeWorkerModelApi::Complete);
    END_PACKAGE
};
struct NativeWorkerApi {
    std::shared_ptr<NativeWorkerModelState> state;
    NativeWorkerApi()=default;
    NativeWorkerApi(NativeWorkersApi*,std::string ticket);
    X::Value Model();
    X::Value Identity();
    BEGIN_PACKAGE(NativeWorkerApi)
        APISET().AddFunc<0>("Identity",&NativeWorkerApi::Identity);
        APISET().AddFunc<0>("Model",&NativeWorkerApi::Model);
    END_PACKAGE
};
struct NativeWorkersApi {
    X::Value Open(std::string ticket);
    BEGIN_PACKAGE(NativeWorkersApi)
        APISET().AddFunc<1>("Open",&NativeWorkersApi::Open);
    END_PACKAGE
};
// Native SDK client, independent of the old worker packet/RPC implementation.
class NativeWorkerModelClient final {
public:
    NativeWorkerModelClient(NativeWorkerModelAccess,std::string root,std::string owner,std::string workspace,std::string generation,std::int64_t revision);
    ~NativeWorkerModelClient();
    AgentWorkerModelAdmission admission(std::stop_token={});
    ModelRequest prepare_request(std::stop_token={});
    ModelRequest rebuild_request(const std::string&,const ModelRequest&,std::stop_token={});
    ModelCompletion complete(const std::string&,const ModelRequest&,ChatCompletionStream::Sink,std::stop_token={});
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
