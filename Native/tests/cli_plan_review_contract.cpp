#include "agentflow/cli_plan_review.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using agentflow::cli::Json;
using agentflow::cli::inspect_plan_human_review;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
Json owner(const std::string& root,const std::string& session,const std::string& label,const std::string& request,std::int64_t revision,std::int64_t sequence){
    const auto run_id="owner_"+label,plan_id="plan_"+label;
    return Json{{"owner_run_id",run_id},{"parent_plan_id","plan_root"},{"parent_node_label",label},{"depth",1},
        {"run",{{"id",run_id},{"session_id",session},{"parent_id",root},{"graph_root",false}}},
        {"plan",{{"id",plan_id},{"root_run_id",root},{"revision",revision},{"state_sequence",sequence},{"state","waiting_human"},{"ready",Json::array()},{"claimed",Json::array()},{"waiting_human",Json::array({"ask"})},{"report_ready",false},{"halted",false}}},
        {"questions",Json::array({{{"id",request},{"plan_id",plan_id},{"root_run_id",root},{"label","ask"},{"question","Confirm branch "+label},{"state","waiting"}}})}};
}
Json snapshot(){
    const std::string root="root_run",session="session";
    return Json{{"run",{{"id",root},{"session_id",session},{"state","paused"},{"parent_id",""},{"graph_root",false}}},
        {"plan",{{"id","plan_root"},{"root_run_id",root},{"revision",3},{"state_sequence",8},{"state","active"},{"ready",Json::array()},{"claimed",Json::array({"left","right"})},{"waiting_human",Json::array()},{"report_ready",false},{"halted",false}}},
        {"questions",Json::array()},{"nested_plans",Json::array({owner(root,session,"left","request_left",11,19),owner(root,session,"right","request_right",22,34)})}};
}
}
int main(){
    try{
        auto value=snapshot();auto review=inspect_plan_human_review(value,"root_run","session");
        require(review.questions.size()==2&&review.waiting.size()==2,"Both sibling questions must be visible in one root review");
        const auto selected=review.waiting.at("request_right");require(selected.plan_id=="plan_right"&&selected.revision==22&&selected.state_sequence==34,"Each nested request must retain its own plan CAS head");
        require(!review.resume_available,"A human-only wait set must not offer a plan resume");
        auto& ready=value["nested_plans"][1];ready["questions"][0]["state"]="answered";ready["plan"]["ready"].push_back("followup");ready["plan"]["report_ready"]=true;
        review=inspect_plan_human_review(value,"root_run","session");require(review.waiting.contains("request_left")&&review.resume_available,"A ready nested continuation must remain resumable while another branch question is pending");
        for(const auto& mutate:std::vector<std::function<void(Json&)>>{
            [](Json& v){v["nested_plans"][1]["questions"][0]["plan_id"]="plan_left";},
            [](Json& v){v["nested_plans"][1]["run"]["parent_id"]="foreign_root";},
            [](Json& v){v["nested_plans"][1]["plan"]["state_sequence"]=9007199254740992LL;}
        }){auto invalid=snapshot();mutate(invalid);bool rejected=false;try{(void)inspect_plan_human_review(invalid,"root_run","session");}catch(const std::exception&){rejected=true;}require(rejected,"Foreign/malformed sibling wait metadata must be rejected");}
        auto duplicate=snapshot();duplicate["nested_plans"][1]["questions"][0]["id"]="request_left";bool rejected=false;try{(void)inspect_plan_human_review(duplicate,"root_run","session");}catch(const std::exception&){rejected=true;}require(rejected,"Duplicate question identity across sibling plans must be rejected");
        std::cout<<"CLI plan review contract passed: both nested human questions, per-plan revision routing, resumable sibling, and fail-closed ownership validation\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
