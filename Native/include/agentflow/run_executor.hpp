#pragma once
#include "agentflow/records.hpp"
#include <utility>
#include <vector>

namespace agentflow {
struct RunBusy : std::runtime_error {using std::runtime_error::runtime_error;};
struct RunUnavailable : std::runtime_error {using std::runtime_error::runtime_error;};
struct ProviderProfileAdmission {std::string id;std::int64_t revision=0;};
// Transport-neutral backend execution boundary; HTTP/CLI do not own run state.
class RunExecutor {
public:
    virtual ~RunExecutor()=default;
    virtual Run submit(std::string id,std::string session_id,std::string prompt)=0;
    virtual std::vector<std::string> models() const {return {};}
    virtual bool supports_profile_admission()const{return false;}
    virtual bool supports_delegation()const{return false;}
    virtual bool supports_dynamic_planning()const{return false;}
    virtual Run plan_input(const std::string&,const std::string&,std::string,const std::string&,std::int64_t,std::int64_t){throw RunUnavailable("Dynamic plan input is unavailable");}
    virtual Run resume_plan(const std::string&,const std::string&,std::int64_t,std::int64_t){throw RunUnavailable("Dynamic plan resume is unavailable");}
    virtual Run submit_profile(std::string,std::string,std::string,std::string,ProviderProfileAdmission){throw RunUnavailable("Provider profile admission is unavailable");}
    virtual Run submit_model(std::string id,std::string session_id,std::string prompt,std::string model_id) {
        if(!model_id.empty()) throw std::invalid_argument("Model selection is unavailable");
        return submit(std::move(id),std::move(session_id),std::move(prompt));
    }
    virtual Run submit_message(std::string,std::string,std::string,std::string,std::string){throw RunUnavailable("Incoming message admission is unavailable");}
    virtual void cancel(const std::string& id)=0;
    virtual bool healthy() const=0;
    virtual bool available() const {return true;}
};
}
