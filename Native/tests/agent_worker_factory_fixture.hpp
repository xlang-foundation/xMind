#include "agentflow/agent_worker_read_executor.hpp"
namespace worker_factory_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
using worker_read_fixture::check;using worker_read_fixture::rejects;using worker_read_fixture::write;
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder){
    for(const std::string mode:{"product_factory","product_factory_read","product_factory_denied","product_factory_attached"}){
    const bool read=mode!="product_factory",denied=mode=="product_factory_denied";
    const auto directory=folder/mode;check(std::filesystem::create_directory(directory),"Production factory requires a fresh workspace");write(directory/"AGENTS.md","FACTORY_REPOSITORY_GUIDANCE\n");
    const auto marker="factory-native-read-"+std::to_string(std::random_device{}())+"\n";write(directory/"same.txt",marker);
    AgentSettings settings;settings.provider={endpoint+"/"+mode,"worker-rpc-fixture",Capability::supported,Capability::supported};settings.provider.output_limit=Capability::supported;settings.max_output_tokens=32;settings.workspace=directory.string();settings.a2a_enabled=false;settings.tool_allowlist=std::vector<std::string>{};settings.instructions="Factory base instructions.";settings.instruction_policy={"Factory backend policy suffix.",9};settings.run_timeout=20s;settings.credential=CredentialReference{"worker","rpc-model","provider:chat"};
    if(denied)settings.permission_rules={{"read_file","**","deny"}};
    settings.provider_identity=ProviderExecutionIdentity{"worker-profile","controlled-route","openai",7};
    if(mode=="product_factory_attached"){const auto attached=folder/"factory-attached-folder";check(std::filesystem::create_directory(attached),"Factory attached folder must be fresh");write(attached/"same.txt",marker);write(directory/"same.txt","Primary folder decoy; this must not be the selected read.\n");WorkspaceTools primary(directory.string()),extra(attached.string());const WorkspaceMembershipSnapshot snapshot{primary.identity(),11,{{primary.identity(),primary.root_path()},{extra.identity(),extra.root_path()}}};settings.workspace_membership_snapshot=[snapshot]{return snapshot;};}
    const auto executor=make_agent_worker_read_executor(store,settings,image);
    {auto unsupported=settings;unsupported.approved_edits=true;rejects<std::invalid_argument>([&]{make_agent_worker_read_executor(store,unsupported,image);});}
    const auto catalog=AgentDefinitionStore(store).apply("agents:\n  - id: factory.worker\n    model_id: worker-rpc-fixture\n    instructions: Factory selected instructions.\n    tools: "+std::string(read?"[read_file]":"[]")+"\n    permissions:\n      - action: read_file\n        resource: '**'\n        effect: allow\n");
    const std::string session="rpc-session-"+mode,id="rpc-"+mode;store.create_session(session,"Production worker factory").get();
    AgentService service(store,settings,1,2,{},executor);const auto definitions=service.agent_definitions();check(definitions.size()==1,"Factory fixture registered one profile");service.select_session_agent(session,definitions.front(),0);service.submit(id,session,"Native child model RPC "+mode+".");
    const auto until=Clock::now()+10s;while(Clock::now()<until&&!service.idle())std::this_thread::sleep_for(5ms);
    check(service.idle()&&service.healthy()&&store.run(id).get().state==RunState::completed,"Production factory must complete and physically drain before service idle");
    const auto history=store.run_history(id).get();check(history.size()==(read?4:2)&&Json::parse(history.back().json).at("content")=="Factory worker complete."&&store.root_budget(id).get().model_calls_reserved==(read?2:1),"Factory must persist actual model results and exact charges");
    if(read){const auto output=Json::parse(Json::parse(history[2].json).at("content").get<std::string>());if(denied)check(output.at("error").at("code")=="agent_permission_denied"&&history[2].json.find(marker)==std::string::npos,"Base denial must survive selected allow without exposing bytes");else check(output.at("content")==marker,"Factory must return exact actual native file bytes");
        const auto events=store.events(id).get();check(std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind=="worker.read.dispatching";})==(denied?0:1),"Factory read dispatch count must respect inherited policy");}
    const auto identity=Json::parse(store.root_budget(id).get().spec.provider_identity_json);check(identity.size()==6&&identity.at("profile_revision")==7&&identity.at("route_id")=="controlled-route","Factory must preserve complete native provider identity");service.close();
    std::cout<<"Production worker factory "<<mode<<" verified: actual AgentService child, captured tools and inherited permissions, exact canonical history, instruction ordering, output control, full provider identity and physical drain."<<std::endl;
    }
}
}
