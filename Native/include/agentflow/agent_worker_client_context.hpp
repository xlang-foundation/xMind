#pragma once
#include "agentflow/agent_worker_channel.hpp"
#include <functional>

namespace agentflow {
class NativeWorkerModelClient;
struct AgentWorkerRpcClosed:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerRpcCancelled:std::runtime_error {using std::runtime_error::runtime_error;};
struct AgentWorkerRpcTimeout:std::runtime_error {using std::runtime_error::runtime_error;};
class AgentWorkerRpcRemoteError:public std::runtime_error {
public:
    AgentWorkerRpcRemoteError(std::string code,std::string detail={})
        :std::runtime_error("Remote native worker call failed"),code_(std::move(code)),detail_(std::move(detail)){}
    const std::string& code()const noexcept{return code_;}
    const std::string& detail()const noexcept{return detail_;}
private:std::string code_,detail_;
};
using AgentWorkerRpcSink=std::function<void(const std::string&)>;
// Captured worker services used by runner adapters. Product instances own only
// native SDK clients; transport fixtures implement call() for fault injection.
class AgentWorkerClientContext {
public:
    virtual ~AgentWorkerClientContext()=default;
    virtual const AgentWorkerScope& scope()const noexcept=0;
    virtual std::shared_ptr<NativeWorkerModelClient> native_model()const=0;
    virtual std::string call(const std::string&,const std::string&,AgentWorkerRpcSink={},std::stop_token={})=0;
};
class NativeWorkerContext final:public AgentWorkerClientContext {
public:
    NativeWorkerContext(AgentWorkerScope scope,std::shared_ptr<NativeWorkerModelClient> client)
        :scope_(std::move(scope)),client_(std::move(client)){
        if(!client_)throw std::invalid_argument("Native worker context requires SDK services");
    }
    const AgentWorkerScope& scope()const noexcept override{return scope_;}
    std::shared_ptr<NativeWorkerModelClient> native_model()const override{return client_;}
    std::string call(const std::string&,const std::string&,AgentWorkerRpcSink={},std::stop_token={})override{
        throw AgentWorkerRpcRemoteError("unsupported");
    }
private:AgentWorkerScope scope_;std::shared_ptr<NativeWorkerModelClient> client_;
};
}
