#include "agentflow/provider_profile_runtime.hpp"
namespace worker_profile_factory_fixture {
using namespace agentflow;using Json=nlohmann::json;using Clock=std::chrono::steady_clock;using namespace std::chrono_literals;
using worker_read_fixture::check;using worker_read_fixture::write;
inline void parent(PersistenceService& store,const std::shared_ptr<const BoundProcessExecutable>& image,const std::string& endpoint,const std::filesystem::path& folder){
    const auto directory=folder/"product_factory_profile";check(std::filesystem::create_directory(directory),"Profile worker factory requires a fresh workspace");write(directory/"AGENTS.md","FACTORY_REPOSITORY_GUIDANCE\n");
    AgentSettings base;base.workspace=directory.string();base.a2a_enabled=false;base.tool_allowlist=std::vector<std::string>{};base.instructions="Factory base instructions.";base.instruction_policy={"Factory backend policy suffix.",9};base.max_output_tokens=32;base.run_timeout=20s;base.provider.endpoint=endpoint+"/product_factory_profile";
    ChatProviderConfig provider{endpoint+"/product_factory_profile","",Capability::supported,Capability::supported};provider.output_limit=Capability::supported;provider.deadline=10s;provider.idle_timeout=10s;
    ProviderProfileRoute route{"factory.chat","openai",provider.endpoint,"worker","factory-profile:chat",ProviderWire::chat_completions};
    std::vector<std::pair<ProviderExecutionIdentity,CredentialReference>> captured;
    ProviderProfileRuntime runtime(store,base,{{route,provider}},1,2,[&](PersistenceService& actual,const AgentSettings& settings){check(&actual==&store&&settings.provider_identity&&settings.credential&&!settings.planning,"Profile factory must receive exact resolved credential and explicit capability policy");captured.emplace_back(*settings.provider_identity,*settings.credential);return make_agent_worker_read_executor(actual,settings,image);});
    check(captured.empty(),"Unconfigured profile runtime cannot construct a fake worker executor");
    AgentDefinitionStore(store).apply("agents:\n  - id: factory.profile.worker\n    model_id: worker-rpc-fixture\n    instructions: Factory selected instructions.\n    tools: []\n");
    const auto secret=[](){const std::string text="synthetic-model-rpc-not-live";return SecretBytes({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});};
    auto metadata=runtime.save_profile("factory-one",route.id,"worker-rpc-fixture",secret(),0,true);
    const auto execute=[&](const std::string& suffix,const std::string& profile){
        const auto identity=captured.back().first;check(identity.profile_id==profile,"Active generation must use selected profile");
        const std::string session="rpc-profile-factory-"+suffix,id=session+"-run";store.create_session(session,"Actual profile worker generation").get();const auto definitions=runtime.agent_definitions();check(definitions.size()==1,"Profile worker fixture requires one definition");runtime.select_session_agent(session,definitions.front(),0);runtime.submit_model(id,session,"Native child model RPC product_factory_profile.","worker-rpc-fixture");
        const auto until=Clock::now()+10s;while(Clock::now()<until&&!runtime.idle())std::this_thread::sleep_for(5ms);check(runtime.idle()&&runtime.healthy()&&store.run(id).get().state==RunState::completed,"Profile worker must complete and physically drain before profile mutation");
        const auto saved=Json::parse(store.root_budget(id).get().spec.provider_identity_json);check(saved.at("profile_id")==profile&&saved.at("profile_revision")==identity.profile_revision&&saved.at("route_id")==route.id&&saved.at("model_id")=="worker-rpc-fixture","Profile worker must retain exact admitted public identity");
        const auto history=store.run_history(id).get();check(history.size()==2&&Json::parse(history.back().json).at("content")=="Factory worker complete."&&store.root_budget(id).get().model_calls_reserved==1,"Profile generation must commit exactly one actual result and charge");
    };
    execute("first","factory-one");
    metadata=runtime.save_profile("factory-two",route.id,"worker-rpc-fixture",secret(),metadata.revision,false);check(metadata.active=="factory-one","Inactive candidate construction cannot replace active profile");
    metadata=runtime.select_profile("factory-two",metadata.revision);execute("second","factory-two");
    metadata=runtime.select_profile("factory-one",metadata.revision);execute("restored","factory-one");
    check(captured.size()==4&&captured[0].first.profile_id=="factory-one"&&captured[1].first.profile_id=="factory-two"&&captured[2].first.profile_id=="factory-two"&&captured[3].first.profile_id=="factory-one"&&captured[0].second.id!=captured[1].second.id&&captured[0].second.id==captured[3].second.id,"Prepared generations must preserve separate encrypted credential references and restore the original binding");
    std::cout<<"Provider-profile worker factory verified: inactive candidate retained old owner; selected second profile then restored first; three actual child roots, four prepared executors, distinct encrypted credential references and exact immutable profile provenance."<<std::endl;
}
}
