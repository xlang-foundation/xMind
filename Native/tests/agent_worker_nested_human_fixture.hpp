// Production factory lifecycle with controlled inference; no live provider.
namespace {
class NestedHumanModel final:public AgentModelGateway {
public:
    explicit NestedHumanModel(bool repeated=false):repeated_(repeated){}
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
        if(child&&repeated_&&calls==2){
            const auto report=std::find_if(request.messages.rbegin(),request.messages.rend(),[&](const auto& message){return message.role==MessageRole::tool&&message.tool_call_id==call;});
            require(report!=request.messages.rend()&&report->content.find("nested-human-answer")!=std::string::npos,"Second pause must consume the original accepted human report");
            const auto observed=Json::parse(report->content);require(std::any_of(request.tools.begin(),request.tools.end(),[](const auto& tool){return tool.name=="revise_plan";}),"Replacement child must retain native plan revision capability");
            return {"","","tool_calls","{}",{{"nested-human-second","revise_plan",Json{{"expected_revision",observed.at("revision")},{"expected_state_sequence",observed.at("state_sequence")},{"add",Json::array({{{"id","answer-second"},{"type","human"},{"question","Synthetic second child-owned question"},{"depends_on",Json::array()}}})}}.dump()}},"[]"};
        }
        const auto expected_call=child&&repeated_?"nested-human-second":call;
        require(calls==(child&&repeated_?3:2)&&std::any_of(request.messages.begin(),request.messages.end(),[&](const auto& message){return message.role==MessageRole::tool&&message.tool_call_id==expected_call&&message.content.find(child?(repeated_?"nested-human-answer-2":"nested-human-answer"):"Nested human child completed")!=std::string::npos;}),"Replacement worker must consume the original accepted report without replay");
        return {child?"Nested human child completed":"Nested human parent completed","","stop","{}",{},"[]"};
    }
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected nested human token count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected nested human compaction");}
private:
    bool repeated_;
};
class PausedRootFairnessModel final:public AgentModelGateway {
    std::mutex mutex_;NestedHumanModel paused_;unsigned roots_=0,children_=0;bool hold_;std::atomic_bool entered_=false,released_=false;
public:
    explicit PausedRootFairnessModel(bool hold=false):hold_(hold){}
    bool entered()const{return entered_.load();}
    void release(){released_=true;}
    ModelCompletion complete(const ChatProviderConfig& config,const ModelRequest& request,ChatCompletionStream::Sink sink,std::stop_token stop)override{
        std::lock_guard lock(mutex_);const auto has=[&](const char* prefix){return std::any_of(request.messages.begin(),request.messages.end(),[&](const auto& message){return message.role==MessageRole::user&&message.content.starts_with(prefix);});};
        const bool child=has("Fairness child B"),other=child||has("Fairness root B");if(!other)return paused_.complete(config,request,std::move(sink),stop);
        auto& calls=child?children_:roots_;++calls;const std::string call=child?"fairness-read":"fairness-child";
        if(hold_&&!child&&calls==1){entered_=true;const auto deadline=std::chrono::steady_clock::now()+10s;while(!released_&&!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(5ms);require(released_&&!stop.stop_requested(),"Unrelated root must retain its active model transport through A cancellation");}
        if(calls==1)return {"","","tool_calls","{}",{{call,"plan_tasks",child?R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"read","type":"tool","tool":"read_file","arguments_json":"{\"path\":\"fairness-input.txt\"}","depends_on":[]}]})":R"({"expected_revision":0,"expected_state_sequence":0,"add":[{"id":"child","type":"agent","objective":"Fairness child B","preset":"workspace.inspect","depends_on":[]}]})"}},"[]"};
        require(calls==2&&std::any_of(request.messages.begin(),request.messages.end(),[&](const auto& message){return message.role==MessageRole::tool&&message.tool_call_id==call&&message.content.find(child?"Actual independent root native read":"Fairness child B completed")!=std::string::npos;}),"Independent root must consume its actual native child/read report once");return {child?"Fairness child B completed":"Fairness root B completed","","stop","{}",{},"[]"};
    }
    std::array<unsigned,4> counts(){std::lock_guard lock(mutex_);return {paused_.roots,paused_.children,roots_,children_};}
    ResponsesTokenCount count_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected fairness count");}
    ResponsesCompactionResult compact_context(const ChatProviderConfig&,const ModelRequest&,std::stop_token)override{throw std::runtime_error("Unexpected fairness compaction");}
};
void production_paused_root_fairness(PersistenceService& store,const std::string& path,std::shared_ptr<const BoundProcessExecutable> image){
    const auto folder=std::filesystem::path(path).parent_path();{std::ofstream file(folder/"fairness-input.txt",std::ios::binary);file<<"Actual independent root native read\n";require(static_cast<bool>(file),"Fairness source write failed");}
    for(const bool coding:{false,true})for(const bool counted:{false,true})for(const bool cancelled:{false,true}){
        const auto suffix=std::string(coding?"coding-":"read-")+(counted?"counted":"plain")+(cancelled?"-cancel":"-resume"),a="fairness-paused-"+suffix,b="fairness-ready-"+suffix;
        auto settings=dynamic_fixture_child(folder.string());settings.planning=DynamicPlanningPolicy{};settings.approved_edits=coding;settings.tool_allowlist.reset();if(counted){settings.context=ContextRuntimePolicy{};settings.context->compaction.automatic=false;}
        auto gateway=std::make_shared<PausedRootFairnessModel>(cancelled);auto executor=coding?make_agent_worker_coding_executor(store,settings,image,gateway):make_agent_worker_read_executor(store,settings,image,gateway);
        AgentService service(store,settings,1,8,{},executor);store.create_session(a,"Paused root A").get();store.create_session(b,"Independent root B").get();service.submit(a,a,"Synthetic nested human root input");
        const auto paused=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<paused&&service.healthy()&&store.run(a).get().state!=RunState::paused)std::this_thread::sleep_for(5ms);require(service.healthy()&&store.run(a).get().state==RunState::paused,"Root A must retain its nested human pause");
        const auto original=store.run_history(a).get();const auto children=store.owned_children(a).get();require(children.size()==1,"Paused root A must retain one child");const auto child=children.front().run.id;const auto plan=store.dynamic_plan_for_owner(child).get();require(plan.has_value(),"Paused child plan missing");const auto requests=store.dynamic_human_requests(plan->id).get();require(requests.size()==1&&requests.front().state=="waiting","Root A must await its original question");
        service.submit(b,b,"Fairness root B");
        if(cancelled){
            const auto entered=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<entered&&service.healthy()&&!gateway->entered())std::this_thread::sleep_for(5ms);
            require(gateway->entered()&&store.run(b).get().state==RunState::running,"B must be physically executing before A cancellation");
            std::string generation;for(const auto& event:store.events(b).get())if(event.kind=="worker.lease.admitted")generation=Json::parse(event.json).at("generation");require(!generation.empty(),"B must have an actual SDK worker lease");const auto lease=store.agent_worker_lease(generation).get();require(lease.state=="active"&&lease.spec.pid!=GetCurrentProcessId(),"B model call must belong to another active process");
            service.cancel(a);require(service.healthy()&&store.run(a).get().state==RunState::cancelled&&store.run(child).get().state==RunState::cancelled&&store.dynamic_human_requests(plan->id).get().front().state=="cancelled","Cancellation must settle A's paused tree and unanswered question");
            const auto retained_lease=store.agent_worker_lease(generation).get();require(store.run(b).get().state==RunState::running&&retained_lease.state=="active"&&retained_lease.spec.pid==lease.spec.pid,"Cancelling A cannot retire or replace B's active worker");gateway->release();
        }
        const auto completed=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<completed&&service.healthy()&&store.run(b).get().state!=RunState::completed)std::this_thread::sleep_for(5ms);require(service.healthy()&&store.run(b).get().state==RunState::completed,"One physical root worker must complete independent B");
        if(cancelled){
            const auto finish=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<finish&&service.healthy()&&!service.idle())std::this_thread::sleep_for(5ms);
            require(service.healthy()&&service.idle()&&gateway->counts()==std::array<unsigned,4>{1,1,2,2}&&store.root_budget(a).get().model_calls_reserved==2&&store.root_budget(b).get().model_calls_reserved==4,"Cancelled A cannot replay inference or consume B's charges");
            const auto retained=store.run_history(a).get();require(retained.size()==original.size(),"Cancelling A cannot add transcript turns");for(std::size_t i=0;i<original.size();++i)require(retained[i].json==original[i].json&&retained[i].sequence==original[i].sequence,"Cancelled A must retain its original transcript");
            require(store.run_history(b).get().size()==4&&store.dynamic_budget_segment(a).get().state=="closed"&&store.dynamic_budget_segment(b).get().state=="closed","Independent completion must retain B history and close both root segments");
            for(const auto& event:store.events(b).get())if(event.kind=="worker.lease.admitted"){const auto lease=store.agent_worker_lease(Json::parse(event.json).at("generation")).get();require(lease.state=="retired"&&lease.retirement_reason=="completed","B SDK worker must retire normally after unrelated cancellation");}
            const auto b_history=store.run_history(b).get();const auto a_events=store.events(a).get(),b_events=store.events(b).get();service.close();
            auto reopened_executor=coding?make_agent_worker_coding_executor(store,settings,image,gateway):make_agent_worker_read_executor(store,settings,image,gateway);
            AgentService reopened(store,settings,1,8,{},reopened_executor);require(reopened.healthy()&&reopened.idle(),"Service reopen cannot adopt terminal roots as active work");
            rejects<NotFound>([&]{reopened.plan_input(a,requests.front().id,R"({"answer":"late-answer-must-not-commit"})","fixture-controller",plan->revision,plan->state_sequence);});
            rejects<NotFound>([&]{reopened.resume_plan(a,"fixture-controller",plan->revision,plan->state_sequence);});
            require(reopened.healthy()&&reopened.idle()&&store.run(a).get().state==RunState::cancelled&&store.run(child).get().state==RunState::cancelled&&store.run(b).get().state==RunState::completed&&gateway->counts()==std::array<unsigned,4>{1,1,2,2},"Late human input and resume cannot resurrect terminal SDK work");
            for(const auto& owner:{a,b}){const auto before=owner==a?original:b_history,after=store.run_history(owner).get();require(before.size()==after.size(),"Service reopen must retain exact terminal history length");for(std::size_t i=0;i<before.size();++i)require(before[i].json==after[i].json&&before[i].sequence==after[i].sequence,"Late input cannot change either root transcript");}
            require(store.events(a).get().size()==a_events.size()&&store.events(b).get().size()==b_events.size()&&store.dynamic_human_requests(plan->id).get().front().state=="cancelled"&&store.root_budget(a).get().model_calls_reserved==2&&store.root_budget(b).get().model_calls_reserved==4,"Reopen and refused late commands cannot append events, answer a cancelled request or reserve model calls");reopened.close();continue;
        }
        require(store.run(a).get().state==RunState::paused&&store.run(child).get().state==RunState::paused&&gateway->counts()==std::array<unsigned,4>{1,1,2,2}&&store.root_budget(a).get().model_calls_reserved==2&&store.root_budget(b).get().model_calls_reserved==4,"Independent root execution cannot replay or charge paused root A");const auto retained=store.run_history(a).get();require(retained.size()==original.size(),"Root B cannot add messages to A");for(std::size_t i=0;i<original.size();++i)require(retained[i].json==original[i].json&&retained[i].sequence==original[i].sequence,"Root A transcript must remain exact");
        require(store.dynamic_human_requests(plan->id).get().front().state=="waiting","Root B cannot answer A's human request");const auto b_history=store.run_history(b).get();require(b_history.size()==4,"Independent B must retain two actual model turns");
        const auto after=store.dynamic_plan_for_owner(child).get();service.plan_input(a,requests.front().id,R"({"answer":"nested-human-answer"})","fixture-controller",after->revision,after->state_sequence);const auto finish=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<finish&&service.healthy()&&!service.idle())std::this_thread::sleep_for(5ms);
        require(service.healthy()&&service.idle()&&store.run(a).get().state==RunState::completed&&store.run(child).get().state==RunState::completed&&gateway->counts()==std::array<unsigned,4>{2,2,2,2},"Original root A must resume once after B completes");require(store.root_budget(a).get().model_calls_reserved==4&&store.root_budget(b).get().model_calls_reserved==4&&store.dynamic_budget_segment(a).get().state=="closed"&&store.dynamic_budget_segment(b).get().state=="closed","Both roots retain independent exact charges and closed segments");service.close();
    }
}
void production_nested_human(PersistenceService& store,const std::string& path){
    auto buffer=std::vector<wchar_t>(32768);const auto length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));require(length&&length<buffer.size(),"Nested human image unavailable");
    const auto image=ForegroundProcess::bind_executable((std::filesystem::path(buffer.data()).parent_path()/"xmind.exe").string());
    for(const bool coding:{false,true})for(const bool counted:{false,true})for(const bool repeated:{false,true}){
        const auto id=std::string("production-nested-human-")+(coding?"coding-":"read-")+(counted?"counted":"plain")+(repeated?"-twice":"-once");
        auto settings=dynamic_fixture_child(std::filesystem::path(path).parent_path().string());settings.planning=DynamicPlanningPolicy{};settings.approved_edits=coding;settings.tool_allowlist.reset();
        if(counted){settings.context=ContextRuntimePolicy{};settings.context->compaction.automatic=false;}
        auto gateway=std::make_shared<NestedHumanModel>(repeated);
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
        if(repeated){
            const auto second_pause=std::chrono::steady_clock::now()+15s;
            bool second_wait=false;while(std::chrono::steady_clock::now()<second_pause&&service->healthy()){
                const auto observed=store.dynamic_human_requests(plan->id).get();second_wait=observed.size()==2&&std::any_of(observed.begin(),observed.end(),[&](const auto& request){return request.id!=requests.front().id&&request.state=="waiting";});
                if(second_wait&&store.run(id).get().state==RunState::paused&&store.run(child).get().state==RunState::paused)break;std::this_thread::sleep_for(5ms);
            }
            require(second_wait,"Second pause must publish a distinct unanswered human request before shutdown");
            if(!(service->healthy()&&store.run(id).get().state==RunState::paused&&store.run(child).get().state==RunState::paused)){
                Json diagnostic={{"root",id},{"child",child},{"healthy",service->healthy()},{"root_state",static_cast<int>(store.run(id).get().state)},{"child_state",static_cast<int>(store.run(child).get().state)},{"events",Json::array()}};
                for(const auto& owner:{id,child}){const auto events=store.events(owner).get();for(auto it=events.size()>12?events.end()-12:events.begin();it!=events.end();++it)diagnostic["events"].push_back({{"owner",owner},{"kind",it->kind},{"json",Json::parse(it->json)}});}std::ofstream out(std::filesystem::path(path).parent_path()/"repeated-pause-diagnostic.json",std::ios::binary);out<<diagnostic.dump(2);throw std::runtime_error("Second child-owned human request must pause the original root and child");
            }
            service->close();service.reset();
            require(gateway->roots==1&&gateway->children==2&&store.root_budget(id).get().model_calls_reserved==3&&store.owned_children(id).get().size()==1&&store.dynamic_budget_segment(id).get().state=="closed","Second pause must preserve one child, exact model charges and close its segment");
            const auto revised=store.dynamic_plan_for_owner(child).get();require(revised.has_value(),"Revised child plan must persist");const auto pending=store.dynamic_human_requests(revised->id).get();
            require(pending.size()==2,"Second pause must retain both original human receipts");const auto answer=std::find_if(pending.begin(),pending.end(),[](const auto& request){return request.state=="waiting";});require(answer!=pending.end()&&answer->id!=requests.front().id,"Only the new child question may await input");
            require(std::any_of(pending.begin(),pending.end(),[&](const auto& request){return request.id==requests.front().id&&request.state=="answered";}),"Second pause cannot replace the first durable human answer");
            const auto wait=store.dynamic_owner_pause(id).get();require(wait.waits.size()==1&&wait.waits.front().plan_id==revised->id,"Current pause head must refer to the same admitted child plan");
            unsigned pause_events=0;for(const auto& event:store.events(id).get())pause_events+=event.kind=="run.paused";require(pause_events==2,"Both original root pause events must remain durable");
            service=std::make_unique<AgentService>(store,settings,1,8,std::shared_ptr<RootAdmissionPool>{},executor());require(service->healthy()&&gateway->roots==1&&gateway->children==2,"Second reopen must adopt without model replay");
            service->plan_input(id,answer->id,R"({"answer":"nested-human-answer-2"})","fixture-controller",revised->revision,revised->state_sequence);
        }
        const auto finish=std::chrono::steady_clock::now()+15s;while(std::chrono::steady_clock::now()<finish&&service->healthy()&&!service->idle())std::this_thread::sleep_for(5ms);
        require(service->healthy()&&service->idle()&&store.run(id).get().state==RunState::completed&&store.run(child).get().state==RunState::completed,"Reopened production factory must resume both child and root to completion");
        require(gateway->roots==2&&gateway->children==(repeated?3:2)&&store.root_budget(id).get().model_calls_reserved==(repeated?5:4)&&store.run_history(id).get().size()==4&&store.run_history(child).get().size()==(repeated?6:4)&&store.owned_children(id).get().size()==1,"Resume must retain exact canonical history and model charges without replacement child admission");
        for(const auto& owner:{id,child}){unsigned paused=0,completed=0;for(const auto& event:store.events(owner).get())if(event.kind=="worker.lease.admitted"){const auto lease=store.agent_worker_lease(Json::parse(event.json).at("generation")).get();require(lease.state=="retired"&&lease.spec.pid!=GetCurrentProcessId(),"Replacement SDK worker must physically retire");paused+=lease.retirement_reason=="paused";completed+=lease.retirement_reason=="completed";}require(paused==(repeated?2:1)&&completed==1,"Each owner must retain its exact paused and completed physical generations");}
        require(store.dynamic_budget_segment(id).get().state=="closed","Completed nested resume must close its replacement segment");service->close();
    }
    production_paused_root_fairness(store,path,image);
}
}
