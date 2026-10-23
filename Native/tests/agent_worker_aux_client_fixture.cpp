#include "agent_worker_rpc_fixture.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/process_executor.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
namespace agentflow {
AgentWorkerServices& FixtureWorkerRpcPeer::services(){if(auto native=native_model())return *native;return *this;}
    std::vector<std::string> FixtureWorkerRpcPeer::callable_names(std::stop_token cancel){const auto result=nlohmann::json::parse(this->call("callable.catalogue","{}"));if(!result.is_array()||result.size()>64)throw ModelProtocolError("Invalid worker callable catalogue");std::vector<std::string> names;for(const auto& entry:result){if(!entry.is_string())throw ModelProtocolError("Invalid worker callable alias");const auto name=entry.get<std::string>();if(name.empty()||name.size()>128||name.find('\0')!=std::string::npos||std::find(names.begin(),names.end(),name)!=names.end())throw ModelProtocolError("Invalid worker callable alias");names.push_back(name);}return names;}
    bool FixtureWorkerRpcPeer::process_join(const std::string& attempt,std::stop_token cancel){
        try{const auto value=nlohmann::json::parse(this->call("process.join",nlohmann::json{{"attempt_id",attempt}}.dump(),{},cancel));
            if(!value.is_object()||value.size()!=1||!value.at("joined").is_boolean())throw ModelProtocolError("Invalid worker process join result");return value.at("joined").get<bool>();}
        catch(const AgentWorkerRpcCancelled&){throw ToolCancelled("Worker process join cancelled");}
        catch(const AgentWorkerRpcTimeout&){throw ToolCancelled("Worker process join deadline elapsed");}
        catch(const AgentWorkerRpcRemoteError& error){if(error.code()=="cancelled"||error.code()=="timeout")throw ToolCancelled("Worker process join cancelled or expired");if(error.code()=="rejected")throw Conflict("Worker process join rejected its captured attempt");if(error.code()=="outcome_unrecorded")throw ProcessOutcomeUnrecorded("Worker process join observation is unrecorded; no retry is authorized");throw ModelProtocolError("Worker process join failed");}
        catch(const nlohmann::json::exception&){throw ModelProtocolError("Invalid worker process join result");}
    }
}
