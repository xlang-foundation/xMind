#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace agentflow::cli {
using Json=nlohmann::json;
struct PlanInputTarget {
    std::string plan_id;
    std::int64_t revision=0,state_sequence=0;
};
struct PlanHumanReview {
    std::vector<Json> questions;
    std::map<std::string,PlanInputTarget> waiting;
    bool resume_available=false;
};

inline std::int64_t plan_counter(const Json& value,const char* field){
    if(!value.is_number_integer()||value<1||value>9007199254740991LL)throw std::runtime_error("Invalid plan counter in observation");
    (void)field;return value.get<std::int64_t>();
}
inline bool safe_plan_identity(const std::string& value){
    return !value.empty()&&value.size()<=128&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
}
inline PlanHumanReview inspect_plan_human_review(const Json& snapshot,const std::string& root,const std::string& session){
    PlanHumanReview result;
    if(!snapshot.is_object()||!snapshot.contains("run")||!snapshot["run"].is_object()||snapshot["run"].value("id",std::string{})!=root||snapshot["run"].value("session_id",std::string{})!=session||!snapshot["run"].value("parent_id",std::string{}).empty()||snapshot["run"].value("graph_root",false)||!snapshot.contains("plan")||!snapshot.contains("questions")||!snapshot["questions"].is_array())throw std::runtime_error("Plan observation ownership changed");
    if(snapshot["plan"].is_null())return result;
    std::set<std::string> plan_ids,request_ids;std::vector<const Json*> owners{&snapshot};
    if(snapshot.contains("nested_plans")){const auto& nested=snapshot["nested_plans"];if(!nested.is_array()||nested.size()>64)throw std::runtime_error("Invalid nested plan observations");for(const auto& owner:nested)owners.push_back(&owner);}
    bool runnable_wait=false;
    for(std::size_t index=0;index<owners.size();++index){
        const auto& owner=*owners[index];if(!owner.is_object()||!owner.contains("plan")||!owner["plan"].is_object()||!owner.contains("questions")||!owner["questions"].is_array())throw std::runtime_error("Invalid plan owner observation");
        const auto& plan=owner["plan"];const auto id=plan.value("id",std::string{});
        if(!safe_plan_identity(id)||plan.value("root_run_id",std::string{})!=root||!plan_ids.insert(id).second||!plan.contains("revision")||!plan.contains("state_sequence")||!plan.at("ready").is_array()||!plan.at("claimed").is_array()||!plan.at("waiting_human").is_array()||!plan.at("report_ready").is_boolean()||!plan.at("halted").is_boolean())throw std::runtime_error("Invalid plan readiness or owner identity");
        const auto revision=plan_counter(plan.at("revision"),"revision"),sequence=plan_counter(plan.at("state_sequence"),"state_sequence");
        if(index>0){
            if(!owner.contains("owner_run_id")||!owner.contains("parent_plan_id")||!owner.contains("parent_node_label")||!owner.contains("depth")||!owner.contains("run")||!owner["run"].is_object())throw std::runtime_error("Nested plan lacks owner metadata");
            const auto& run=owner["run"];const auto owner_id=owner.value("owner_run_id",std::string{});const auto depth=owner.at("depth");
            if(!safe_plan_identity(owner_id)||run.value("id",std::string{})!=owner_id||run.value("session_id",std::string{})!=session||run.value("parent_id",std::string{})!=root||run.value("graph_root",false)||!safe_plan_identity(owner.value("parent_plan_id",std::string{}))||owner.value("parent_node_label",std::string{}).empty()||owner.value("parent_node_label",std::string{}).size()>64||!depth.is_number_integer()||depth<1||depth>8||!plan_ids.contains(owner.value("parent_plan_id",std::string{})))throw std::runtime_error("Nested plan owner path changed");
        }
        if(plan.value("state",std::string{})=="waiting_human"&&!plan.at("halted").get<bool>()&&(!plan.at("ready").empty()||plan.at("report_ready").get<bool>()))runnable_wait=true;
        for(const auto& question:owner["questions"]){
            if(!question.is_object()||question.value("state",std::string{})!="waiting")continue;
            const auto request=question.value("id",std::string{});
            const auto label=question.value("label",std::string{});
            if(!safe_plan_identity(request)||!safe_plan_identity(label)||question.value("root_run_id",std::string{})!=root||question.value("plan_id",std::string{})!=id||std::find(plan.at("waiting_human").begin(),plan.at("waiting_human").end(),label)==plan.at("waiting_human").end()||!question.contains("question")||!question["question"].is_string()||!request_ids.insert(request).second||request_ids.size()>64)throw std::runtime_error("Invalid plan human ownership");
            result.waiting.emplace(request,PlanInputTarget{id,revision,sequence});
            result.questions.push_back(Json{{"id",request},{"label",label},{"owner_run_id",index==0?root:owner.value("owner_run_id",std::string{})},{"plan_id",id},{"revision",revision},{"state_sequence",sequence},{"question",question.at("question")}});
        }
    }
    result.resume_available=snapshot["run"].value("state",std::string{})=="paused"&&runnable_wait;
    return result;
}
}
