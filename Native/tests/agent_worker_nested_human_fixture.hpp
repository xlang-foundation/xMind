// Production factory lifecycle with controlled inference; no live provider.
namespace {
class NestedHumanModel final:public AgentModelGateway {
public:
    unsigned roots=0,children=0;
    ModelCompletion complete(const ChatProviderConfig&,const ModelRequest& request,ChatCompletionStream::Sink,std::stop_token)override{
        const bool child=std::any_of(request.messages.begin(),request.messages.end(),[](const auto& message){return message.role==MessageRole::user&&message.content.starts_with("Nested human child objective");});
        auto& calls=child?children:roots;++calls;
        const std::string call=child?"nested-human-call":"nested-parent-call";
        if(calls==1){
            require(std::any_of(request.tools.begin(),request.tools.end(),[](const auto& tool){return tool.name=="plan_tasks";}),"Nested human owner must receive scoped planning");
            return {"","","tool_calls","{}",{{call,"plan_tasks",child?
                R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"answer","type":"human","question":"Synthetic child-owned question","depends_on":[]}]})":
                R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"child","type":"agent","objective":"Nested human child objective","preset":"workspace.inspect","depends_on":[]}]})"}},"[]"};
        }
        require(calls==2&&std::any_of(request.messages.begin(),request.messages.end(),[&](const auto& message){return message.role==MessageRole::tool&&message.tool_call_id==call&&message.content.find(child?"nested-human-answer":"Nested human child completed")!=std::string::npos;}),"Replacement worker must consume the original accepted report without replay");
        return {child?"Nested human child completed":"Nested human parent completed","","stop","{}",{},"[]"};
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected nested human token count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected nested human compaction");}
};
void production_nested_human(PersistenceService& store,const std::string& path){
    auto buffer=std::vector<wchar_t>(32768);const auto length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));require(length&&length<buffer.size(),"Nested human image unavailable");
    const auto image=ForegroundProcess::bind_executable((std::filesystem::path(buffer.data()).parent_path()/"xmind.exe").string());
    for(const bool coding:{false,true})for(const bool counted:{false,true}){
        const auto id=std::string("production-nested-human-")+(coding?"coding-":"read-")+(counted?"counted":"plain");
        auto settings=dynamic_fixture_child(std::filesystem::path(path).parent_path().string());settings.planning=DynamicPlanningPolicy{};settings.approved_edits=coding;settings.tool_allowlist.reset();
        if(counted){settings.context=ContextRuntimePolicy{};settings.context->compaction.automatic=false;}
        auto gateway=std::make_shared<NestedHumanModel>();
        const auto executor=[&]{return coding?make_agent_worker_coding_executor(store,settings,image,gateway):make_agent_worker_read_executor(store,settings,image,gateway);};
        auto service=std::make_unique<AgentService>(store,settings,1,8,std::shared_ptr<RootAdmissionPool>{},executor());store.create_session(id,"Actual nested SDK human lifecycle").get();service->submit(id,id,"Synthetic nested human root input");
        const auto until=std::chrono::steady_clock::now()+15s;
        while(std::chrono::steady_clock::now()<until&&service->healthy()&&store.run(id).get().state==RunState::queued)std::this_thread::sleep_for(5ms);
        while(std::chrono::steady_clock::now()<until&&service->healthy()&&store.run(id).get().state==RunState::running)std::this_thread::sleep_for(5ms);
        require(service->healthy()&&store.run(id).get().state==RunState::paused,"Production nested human chain must pause");service->close();service.reset();
        const auto children=store.owned_children(id).get();require(children.size()==1&&children.front().run.state==RunState::paused,"Nested pause must retain exactly its paused SDK child");const auto child=children.front().run.id;
        const auto plan=store.dynamic_plan_for_owner(child).get();require(plan.has_value(),"Child-owned human plan must persist");const auto requests=store.dynamic_human_requests(plan->id).get();require(requests.size()==1&&requests.front().state=="waiting","Child-owned human request must remain unanswered");
        require(gateway->roots==1&&gateway->children==1&&store.root_budget(id).get().model_calls_reserved==2&&store.dynamic_budget_segment(id).get().state=="closed","Pause must retain exactly the two original model charges and close its active segment");
        for(const auto& owner:{id,child})for(const auto& event:store.events(owner).get())if(event.kind=="worker.lease.admitted"){const auto lease=store.agent_worker_lease(Json::parse(event.json).at("generation")).get();require(lease.state=="retired"&&lease.retirement_reason=="paused"&&lease.spec.pid!=GetCurrentProcessId(),"Both exact SDK workers must physically drain before reopen");}
        service=std::make_unique<AgentService>(store,settings,1,8,std::shared_ptr<RootAdmissionPool>{},executor());require(service->healthy()&&gateway->roots==1&&gateway->children==1,"Reopen must adopt without inference replay");
        service->plan_input(id,requests.front().id,R"({"answer":"nested-human-answer"})","fixture-controller",plan->revision,plan->state_sequence);
        const auto finish=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<finish&&service->healthy()&&!service->idle())std::this_thread::sleep_for(5ms);
        require(service->healthy()&&service->idle()&&store.run(id).get().state==RunState::completed&&store.run(child).get().state==RunState::completed,"Reopened production factory must resume both child and root to completion");
        require(gateway->roots==2&&gateway->children==2&&store.root_budget(id).get().model_calls_reserved==4&&store.run_history(id).get().size()==4&&store.run_history(child).get().size()==4&&store.owned_children(id).get().size()==1,"Resume must retain exact canonical history and four charges without replacement child admission");
        for(const auto& owner:{id,child}){unsigned paused=0,completed=0;for(const auto& event:store.events(owner).get())if(event.kind=="worker.lease.admitted"){const auto lease=store.agent_worker_lease(Json::parse(event.json).at("generation")).get();require(lease.state=="retired"&&lease.spec.pid!=GetCurrentProcessId(),"Replacement SDK worker must physically retire");paused+=lease.retirement_reason=="paused";completed+=lease.retirement_reason=="completed";}require(paused==1&&completed==1,"Each owner must retain one paused and one completed physical generation");}
        require(store.dynamic_budget_segment(id).get().state=="closed","Completed nested resume must close its replacement segment");service->close();
    }
}
}
