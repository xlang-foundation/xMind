#pragma once
#include <array>
#include <mutex>

// Controlled inference through the production worker factory. Each owner gets
// one planning turn before shutdown and exactly one continuation after reopen.
namespace {
class DeepHumanModel final:public AgentModelGateway {
    std::mutex mutex_;std::array<unsigned,4> calls_{};
public:
    std::array<unsigned,4> counts(){std::lock_guard lock(mutex_);return calls_;}
    ModelCompletion complete(const ChatProviderConfig&,const ModelRequest& request,ChatCompletionStream::Sink,std::stop_token)override{
        const std::string prefix="Deep human child level ";unsigned level=0;
        for(const auto& message:request.messages)if(message.role==MessageRole::user&&message.content.starts_with(prefix)){
            require(message.content.size()>prefix.size()&&message.content[prefix.size()]>='1'&&message.content[prefix.size()]<='3',"Deep fixture child level must remain bounded");level=message.content[prefix.size()]-'0';break;
        }
        unsigned call;{std::lock_guard lock(mutex_);call=++calls_.at(level);}
        const auto id="deep-plan-"+std::to_string(level);
        if(call==1){
            require(std::any_of(request.tools.begin(),request.tools.end(),[](const auto& tool){return tool.name=="plan_tasks";}),"Every deep owner must retain native planning");
            Json node=level==3?Json{{"id","answer"},{"type","human"},{"question","Synthetic deepest-owner question"},{"depends_on",Json::array()}}:Json{{"id","child"},{"type","agent"},{"objective",prefix+std::to_string(level+1)},{"preset","workspace.inspect"},{"depends_on",Json::array()}};
            return {"","","tool_calls","{}",{{id,"plan_tasks",Json{{"expected_revision",0},{"expected_state_sequence",0},{"add",Json::array({node})}}.dump()}},"[]"};
        }
        const auto expected=level==3?std::string("deep-human-answer"):"Deep level "+std::to_string(level+1)+" complete";
        require(call==2&&std::any_of(request.messages.begin(),request.messages.end(),[&](const auto& message){return message.role==MessageRole::tool&&message.tool_call_id==id&&message.content.find(expected)!=std::string::npos;}),"Deep continuation must consume its original child or human receipt exactly once");
        return {"Deep level "+std::to_string(level)+" complete","","stop","{}",{},"[]"};
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected deep plain token count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected deep plain compaction");}
};

void production_deep_human(const std::string& path,const std::vector<std::string>& imports){
    wchar_t buffer[32768]{};const auto length=GetModuleFileNameW(nullptr,buffer,32768);require(length&&length<32768,"Deep human image unavailable");
    const auto image=ForegroundProcess::bind_executable((std::filesystem::path(buffer).parent_path()/"xmind.exe").string());
    for(const bool coding:{false,true}){
        const auto id=std::string("production-deep-human-")+(coding?"coding":"read"),database=path+"-"+(coding?"coding":"read")+".sqlite";NestedCaseTiming timing(id);
        auto settings=dynamic_fixture_child(std::filesystem::path(path).parent_path().string());settings.planning=DynamicPlanningPolicy{};settings.approved_edits=coding;settings.tool_allowlist.reset();
        auto gateway=std::make_shared<DeepHumanModel>();auto store=std::make_unique<PersistenceService>(database,imports);
        const auto executor=[&]{return coding?make_agent_worker_coding_executor(*store,settings,image,gateway):make_agent_worker_read_executor(*store,settings,image,gateway);};
        auto root_executor=executor();auto service=std::make_unique<AgentService>(*store,settings,1,8,std::shared_ptr<RootAdmissionPool>{},root_executor);store->create_session(id,"Three-generation SDK human recovery").get();service->submit(id,id,"Deep human root objective");
        const auto until=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<until&&service->healthy()&&(store->run(id).get().state==RunState::queued||store->run(id).get().state==RunState::running))std::this_thread::sleep_for(5ms);
        require(service->healthy()&&store->run(id).get().state==RunState::paused,"Three-generation chain must propagate its deepest human pause");service->close();service.reset();root_executor.reset();
        std::vector<std::string> owners{id};for(unsigned level=0;level<3;++level){const auto plan=store->dynamic_plan_for_owner(owners.back()).get();require(plan&&plan->root_run_id==id&&plan->owner_run_id==owners.back()&&plan->nodes.size()==1&&!plan->nodes.front().child_run_id.empty(),"Each deep ancestor must retain its exact root, immediate owner and original child");owners.push_back(plan->nodes.front().child_run_id);require(store->run(owners.back()).get().parent_id==id,"Deep child budget parent must remain its ultimate root");}
        require(store->owned_children(id).get().size()==3&&gateway->counts()==std::array<unsigned,4>{1,1,1,1}&&store->root_budget(id).get().model_calls_reserved==4,"Pause must retain three children and exactly four original inferences");
        const auto plan=store->dynamic_plan_for_owner(owners.back()).get();require(plan.has_value(),"Deepest human plan must persist");const auto requests=store->dynamic_human_requests(plan->id).get();require(requests.size()==1&&requests.front().state=="waiting","Deepest request must remain unanswered");
        std::vector<std::vector<Message>> histories;for(const auto& owner:owners){require(store->run(owner).get().state==RunState::paused,"Every ancestor must persist paused state");histories.push_back(store->run_history(owner).get());for(const auto& event:store->events(owner).get())if(event.kind=="worker.lease.admitted"){const auto lease=store->agent_worker_lease(Json::parse(event.json).at("generation")).get();require(lease.state=="retired"&&lease.retirement_reason=="paused"&&lease.spec.pid!=GetCurrentProcessId(),"Each deep worker must physically retire before database reopen");}}
        require(store->dynamic_budget_segment(id).get().state=="closed","Deep pause must close its active budget segment");store->close();store.reset();
        store=std::make_unique<PersistenceService>(database,imports);root_executor=executor();service=std::make_unique<AgentService>(*store,settings,1,8,std::shared_ptr<RootAdmissionPool>{},root_executor);require(service->healthy()&&gateway->counts()==std::array<unsigned,4>{1,1,1,1},"Database/service reopen cannot dispatch another inference");
        for(std::size_t i=0;i<owners.size();++i){const auto after=store->run_history(owners[i]).get();require(after.size()==histories[i].size(),"Database reopen must retain deep history length");for(std::size_t j=0;j<after.size();++j)require(after[j].json==histories[i][j].json&&after[j].sequence==histories[i][j].sequence,"Database reopen must preserve every original deep history row");}
        service->plan_input(id,requests.front().id,R"({"answer":"deep-human-answer"})","fixture-controller",plan->revision,plan->state_sequence);
        const auto finish=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<finish&&service->healthy()&&!service->idle())std::this_thread::sleep_for(5ms);
        if(!(service->healthy()&&service->idle()&&gateway->counts()==std::array<unsigned,4>{2,2,2,2})){Json detail={{"healthy",service->healthy()},{"idle",service->idle()},{"counts",gateway->counts()},{"owners",Json::array()}};if(const auto failure=root_executor->shared_children()->failure())try{std::rethrow_exception(failure);}catch(const std::exception& error){detail["first_child_failure"]=error.what();}for(const auto& owner:owners){Json item={{"id",owner},{"state",static_cast<int>(store->run(owner).get().state)},{"events",Json::array()}};const auto events=store->events(owner).get();for(auto event=events.size()>12?events.end()-12:events.begin();event!=events.end();++event)item["events"].push_back({{"kind",event->kind},{"json",Json::parse(event->json)}});detail["owners"].push_back(item);}std::ofstream output(std::filesystem::path(database).parent_path()/"deep-resume-diagnostic.json");output<<detail.dump(2);std::cerr<<detail.dump()<<std::endl;}
        require(service->healthy()&&service->idle()&&gateway->counts()==std::array<unsigned,4>{2,2,2,2}&&store->root_budget(id).get().model_calls_reserved==8&&store->owned_children(id).get().size()==3,"Deep resume must complete eight exact inferences without replacement child admission");
        for(const auto& owner:owners){require(store->run(owner).get().state==RunState::completed&&store->run_history(owner).get().size()==4,"Each original deep owner must finish with four canonical history rows");unsigned paused=0,completed=0;for(const auto& event:store->events(owner).get())if(event.kind=="worker.lease.admitted"){const auto lease=store->agent_worker_lease(Json::parse(event.json).at("generation")).get();require(lease.state=="retired"&&lease.spec.pid!=GetCurrentProcessId(),"Every replacement deep worker must physically retire");paused+=lease.retirement_reason=="paused";completed+=lease.retirement_reason=="completed";}require(paused==1&&completed==1,"Every owner must retain one original paused and one completed generation");}
        require(store->dynamic_budget_segment(id).get().state=="closed","Deep completion must close its replacement segment");service->close();service.reset();store->close();
    }
}
}
