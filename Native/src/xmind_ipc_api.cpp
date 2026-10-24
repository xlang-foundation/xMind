#include "connection_run_input.hpp"
#include "agentflow/agent_worker_native_execution.hpp"
#include "agentflow/agent_worker_native_links.hpp"
#include "agentflow/agent_worker_native_planning.hpp"
#include "agentflow/xmind_ipc_api.hpp"
#include "agentflow/agent_worker_native_model.hpp"
#include "agentflow/xmind_ipc_runtime.hpp"
#include "agentflow/python_tool_ipc_api.hpp"
#include "agentflow/schema_worker_service.hpp"
#include "agentflow/persistence_service.hpp"
#include "agentflow/run_executor.hpp"
#include "agentflow/local_auth.hpp"
#include "agentflow/backend_owner_control.hpp"
#include "agentflow/connection_profiles.hpp"
#include "agentflow/connection_binding.hpp"
#include "connection_binding_values.hpp"
#include "agent_worker_sdk_values.hpp"
#include <optional>
#include <mutex>
#include "xlang3/xlang3.h"
#include <stdexcept>
namespace agentflow {
namespace {
struct ApiContext {
    PersistenceService& store;
    RunExecutor* executor;
    std::string token;
    ConnectionProfiles connections;
    LocalConnectionBindings bindings;
    BackendOwnerControl* owner=nullptr;
    mutable std::shared_mutex lifetime;
    bool active=true;
    ApiContext(PersistenceService& persistence,RunExecutor* execution,const std::string& auth,BackendOwnerControl* control):store(persistence),executor(execution),token(auth),connections(persistence),bindings(persistence,execution,control),owner(control){}
    auto enter()const{std::shared_lock lock(lifetime);if(!active)throw RunUnavailable("unavailable");return lock;}
    std::optional<std::shared_lock<std::shared_mutex>> admit(){if(owner)return owner->admit();return {};}
    void authorize(const std::string& supplied)const {
        // The existing local owner token grants integration access, never
        // arbitrary SQL, credentials or a worker's private execution authority.
        auto lifetime_lock=enter();
        if(supplied.size()!=token.size())throw std::runtime_error("unauthorized");
        unsigned different=0;for(std::size_t i=0;i<token.size();++i)different|=static_cast<unsigned char>(token[i])^static_cast<unsigned char>(supplied[i]);
        if(different)throw std::runtime_error("unauthorized");
    }
};
template<class F>auto api_call(F action)->decltype(action()){
    try{return action();}catch(const NotFound&){throw std::runtime_error("not_found");}catch(const Conflict&){throw std::runtime_error("conflict");}catch(const BackendQuiesced&){throw std::runtime_error("backend_quiesced");}catch(const RunBusy&){throw std::runtime_error("busy");}catch(const RunUnavailable&){throw std::runtime_error("unavailable");}catch(const std::invalid_argument&){throw std::runtime_error("invalid_arguments");}catch(...){throw std::runtime_error("internal_error");}
}
void set(X::Value& value,const char* name,const X::Value& field){if(!value.Set(name,field))throw std::runtime_error("Cannot construct xMind API record");}
X::Value session_value(X3PackageHost* host,const Session& session){auto value=X::Value::Dict(host);set(value,"id",X::Value::String(host,session.id));set(value,"title",X::Value::String(host,session.title));set(value,"parent_id",X::Value::String(host,session.parent_id));return value;}
X::Value run_value(X3PackageHost* host,const Run& run){auto value=X::Value::Dict(host);set(value,"id",X::Value::String(host,run.id));set(value,"session_id",X::Value::String(host,run.session_id));set(value,"state",X::Value::String(host,to_string(run.state)));set(value,"parent_id",X::Value::String(host,run.parent_id));set(value,"node_id",X::Value::String(host,run.node_id));set(value,"graph_root",X::Value(run.graph_root));return value;}
struct RootApi;
struct HostApi {
    explicit HostApi(RootApi*){}
    std::int64_t ApiVersion()const{return 1;}
    std::string Transport()const{return "xlang3-lrpc";}
    BEGIN_PACKAGE(HostApi)
        APISET().AddFunc<0>("ApiVersion",&HostApi::ApiVersion);
        APISET().AddFunc<0>("Transport",&HostApi::Transport);
    END_PACKAGE
};
struct SessionsApi {
    std::shared_ptr<ApiContext> context;
    SessionsApi(RootApi*,std::string token);
    X::Value Open(std::string id){return api_call([&]{auto lifetime_lock=context->enter();return session_value(Host(),context->store.session(std::move(id)).get());});}
    X::Value Create(std::string id,std::string title){return api_call([&]{auto lifetime_lock=context->enter();auto admission=context->admit();return session_value(Host(),context->store.create_session(std::move(id),std::move(title)).get());});}
    BEGIN_PACKAGE(SessionsApi)
        APISET().AddFunc<1>("Open",&SessionsApi::Open);
        APISET().AddFunc<2>("Create",&SessionsApi::Create);
    END_PACKAGE
};
struct RunsApi {
    std::shared_ptr<ApiContext> context;
    RunsApi(RootApi*,std::string token);
    X::Value Open(std::string id){return api_call([&]{auto lifetime_lock=context->enter();return run_value(Host(),context->store.run(std::move(id)).get());});}
    X::Value Start(std::string id,std::string session,std::string prompt){return api_call([&]{auto lifetime_lock=context->enter();auto admission=context->admit();if(!context->executor)throw RunUnavailable("Run execution unavailable");return run_value(Host(),context->executor->submit(std::move(id),std::move(session),std::move(prompt)));});}
    void Cancel(std::string id){api_call([&]{auto lifetime_lock=context->enter();auto admission=context->admit();if(!context->executor)throw RunUnavailable("Run execution unavailable");context->executor->cancel(id);});}
    BEGIN_PACKAGE(RunsApi)
        APISET().AddFunc<1>("Open",&RunsApi::Open);
        APISET().AddFunc<3>("Start",&RunsApi::Start);
        APISET().AddFunc<1>("Cancel",&RunsApi::Cancel);
    END_PACKAGE
};
X::Value bound_value(X3PackageHost* host,const BoundConnectionSession& value){return sdk_values::to_value(host,connection_values::bound(value));}
X::Value bound_value(X3PackageHost* host,const BoundConnectionRun& value){return sdk_values::to_value(host,connection_values::bound(value));}
struct BoundSessionsApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundSessionsApi()=default;explicit BoundSessionsApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    void check()const{if(!state)throw RunUnavailable("Unbound connection service");}
    X::Value Open(std::string id){return api_call([&]{check();return bound_value(Host(),state->open_session(id));});}
    X::Value Create(std::string id,std::string title){return api_call([&]{check();return bound_value(Host(),state->create_session(id,title));});}
    X::Value Messages(std::string id,std::int64_t before,std::int64_t count){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::bound(state->messages(id,before,count)));});}
    BEGIN_PACKAGE(BoundSessionsApi) APISET().AddFunc<1>("Open",&BoundSessionsApi::Open);APISET().AddFunc<2>("Create",&BoundSessionsApi::Create);APISET().AddFunc<3>("Messages",&BoundSessionsApi::Messages); END_PACKAGE
};
struct BoundRunsApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundRunsApi()=default;explicit BoundRunsApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    void check()const{if(!state)throw RunUnavailable("Unbound connection service");}
    X::Value Open(std::string id){return api_call([&]{check();return bound_value(Host(),state->open_run(id));});}
    X::Value ChildHistory(std::string session,std::string parent,std::string child){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::bound(state->child_history(session,parent,child)));});}
    X::Value History(std::string session,std::string run,std::int64_t before,std::int64_t count){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::bound(state->run_messages(session,run,before,count)));});}
    X::Value Start(std::string id,std::string session,std::string prompt){return api_call([&]{check();return bound_value(Host(),state->start_run(id,session,prompt));});}
    void Cancel(std::string id){api_call([&]{check();state->cancel_run(id);});}
    X::Value Submit(X::Value request){return api_call([&]{check();return bound_value(Host(),state->start_run(connection_input::run_request(sdk_values::from_value(request))));});}
    BEGIN_PACKAGE(BoundRunsApi) APISET().AddFunc<3>("ChildHistory",&BoundRunsApi::ChildHistory);APISET().AddFunc<4>("History",&BoundRunsApi::History);APISET().AddFunc<1>("Submit",&BoundRunsApi::Submit);APISET().AddFunc<1>("Open",&BoundRunsApi::Open);APISET().AddFunc<3>("Start",&BoundRunsApi::Start);APISET().AddFunc<1>("Cancel",&BoundRunsApi::Cancel); END_PACKAGE
};
struct BoundWorkspaceApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundWorkspaceApi()=default;explicit BoundWorkspaceApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    X::Value Info(){return api_call([&]{if(!state)throw RunUnavailable("Unbound workspace service");return sdk_values::to_value(Host(),connection_values::bound(state->workspace()));});}
    X::Value Membership(){return api_call([&]{if(!state)throw RunUnavailable("Unbound workspace service");return sdk_values::to_value(Host(),connection_values::bound(state->membership()));});}
    BEGIN_PACKAGE(BoundWorkspaceApi) APISET().AddFunc<0>("Info",&BoundWorkspaceApi::Info);APISET().AddFunc<0>("Membership",&BoundWorkspaceApi::Membership); END_PACKAGE
};
struct BoundCommandsApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundCommandsApi()=default;explicit BoundCommandsApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    X::Value List(){return api_call([&]{if(!state)throw RunUnavailable("Unbound command service");return sdk_values::to_value(Host(),connection_values::bound(state->commands()));});}
    X::Value Invoke(std::string session,X::Value request){return api_call([&]{if(!state)throw RunUnavailable("Unbound command service");return bound_value(Host(),state->invoke_command(connection_input::command_request(sdk_values::from_value(request),std::move(session))));});}
    BEGIN_PACKAGE(BoundCommandsApi) APISET().AddFunc<0>("List",&BoundCommandsApi::List);APISET().AddFunc<2>("Invoke",&BoundCommandsApi::Invoke); END_PACKAGE
};
struct BoundModelsApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundModelsApi()=default;explicit BoundModelsApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    X::Value List(){return api_call([&]{if(!state)throw RunUnavailable("Unbound model service");return sdk_values::to_value(Host(),connection_values::bound(state->models()));});}
    X::Value Open(std::string session){return api_call([&]{if(!state)throw RunUnavailable("Unbound model service");return sdk_values::to_value(Host(),connection_values::bound(state->session_model(session)));});}
    X::Value Select(std::string session,X::Value request){return api_call([&]{if(!state)throw RunUnavailable("Unbound model service");return sdk_values::to_value(Host(),connection_values::bound(state->select_session_model(session,connection_input::model_selection(sdk_values::from_value(request)))));});}
    BEGIN_PACKAGE(BoundModelsApi) APISET().AddFunc<0>("List",&BoundModelsApi::List);APISET().AddFunc<1>("Open",&BoundModelsApi::Open);APISET().AddFunc<2>("Select",&BoundModelsApi::Select); END_PACKAGE
};
struct BoundGraphsApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundGraphsApi()=default;explicit BoundGraphsApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    X::Value Submit(X::Value request){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");return bound_value(Host(),state->start_graph(connection_input::graph_request(sdk_values::from_value(request))));});}
    X::Value List(){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");return sdk_values::to_value(Host(),connection_values::bound(state->graphs()));});}
    X::Value Open(std::string id){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");return sdk_values::to_value(Host(),connection_values::bound(state->graph(id)));});}
    X::Value Children(std::string id){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");return sdk_values::to_value(Host(),connection_values::bound(state->graph_children(id)));});}
    X::Value Input(std::string root,std::string node,std::string input,std::int64_t revision){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");return sdk_values::to_value(Host(),connection_values::bound(state->graph_input(root,{},node,input,revision)));});}
    X::Value TreeInput(std::string root,std::string owner,std::string node,std::string input,std::int64_t revision,std::int64_t owner_revision){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");if(owner.empty())throw std::invalid_argument("Nested graph owner is required");return sdk_values::to_value(Host(),connection_values::bound(state->graph_input(root,owner,node,input,revision,owner_revision)));});}
    X::Value Resume(std::string root,std::int64_t revision){return api_call([&]{if(!state)throw RunUnavailable("Unbound graph service");return bound_value(Host(),state->resume_graph(root,revision));});}
    BEGIN_PACKAGE(BoundGraphsApi) APISET().AddFunc<1>("Children",&BoundGraphsApi::Children);APISET().AddFunc<4>("Input",&BoundGraphsApi::Input);APISET().AddFunc<6>("TreeInput",&BoundGraphsApi::TreeInput);APISET().AddFunc<2>("Resume",&BoundGraphsApi::Resume);APISET().AddFunc<0>("List",&BoundGraphsApi::List);APISET().AddFunc<1>("Open",&BoundGraphsApi::Open);APISET().AddFunc<1>("Submit",&BoundGraphsApi::Submit); END_PACKAGE
};
struct BoundOperationsApi {
    std::shared_ptr<CapturedRunOperations> state;BoundOperationsApi()=default;explicit BoundOperationsApi(std::shared_ptr<CapturedRunOperations> value):state(std::move(value)){}
    void check()const{if(!state)throw RunUnavailable("Unbound operation scope");}
    X::Value List(){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::bound(state->list()));});}
    X::Value Open(std::string id){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::bound(state->open(id)));});}
    X::Value Decide(std::string id,std::string decision){return api_call([&]{check();if(decision!="allow"&&decision!="deny")throw std::invalid_argument("Decision must be allow or deny");return sdk_values::to_value(Host(),connection_values::bound(state->decide(id,decision=="allow"?OperationDecision::allow:OperationDecision::deny)));});}
    BEGIN_PACKAGE(BoundOperationsApi) APISET().AddFunc<0>("List",&BoundOperationsApi::List);APISET().AddFunc<1>("Open",&BoundOperationsApi::Open);APISET().AddFunc<2>("Decide",&BoundOperationsApi::Decide); END_PACKAGE
};
struct BoundEventsApi {
    std::shared_ptr<ConnectionEventStream> state;BoundEventsApi()=default;explicit BoundEventsApi(std::shared_ptr<ConnectionEventStream> value):state(std::move(value)){}
    void check()const{if(!state)throw RunUnavailable("Unbound connection service");}
    X::Value Cursor(){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::cursor(state->cursor()));});}
    X::Value Read(X::Value position){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::page(state->read(connection_values::cursor(sdk_values::from_value(position)))));});}
    BEGIN_PACKAGE(BoundEventsApi) APISET().AddFunc<0>("Cursor",&BoundEventsApi::Cursor);APISET().AddFunc<1>("Read",&BoundEventsApi::Read); END_PACKAGE
};
struct BoundConnectionApi {
    std::shared_ptr<CapturedLocalConnection> state;BoundConnectionApi()=default;explicit BoundConnectionApi(std::shared_ptr<CapturedLocalConnection> value):state(std::move(value)){}
    void check()const{if(!state)throw RunUnavailable("Unbound connection");}
    X::Value Identity(){return api_call([&]{check();return sdk_values::to_value(Host(),connection_values::identity(state->identity()));});}
    X::Value Sessions();X::Value Runs();X::Value Graphs();X::Value Models();X::Value Commands();X::Value Workspace();X::Value Events(std::string session,std::string root);X::Value RunEvents(std::string session,std::string run);X::Value Operations(std::string session,std::string run);
    BEGIN_PACKAGE(BoundConnectionApi) APISET().AddFunc<0>("Workspace",&BoundConnectionApi::Workspace);APISET().AddFunc<0>("Commands",&BoundConnectionApi::Commands);APISET().AddFunc<0>("Models",&BoundConnectionApi::Models);APISET().AddFunc<0>("Graphs",&BoundConnectionApi::Graphs);APISET().AddFunc<0>("Identity",&BoundConnectionApi::Identity);APISET().AddFunc<0>("Sessions",&BoundConnectionApi::Sessions);APISET().AddFunc<0>("Runs",&BoundConnectionApi::Runs);APISET().AddFunc<2>("Events",&BoundConnectionApi::Events);APISET().AddFunc<2>("RunEvents",&BoundConnectionApi::RunEvents);APISET().AddFunc<2>("Operations",&BoundConnectionApi::Operations); END_PACKAGE
};
struct ConnectionObjects {BEGIN_PACKAGE(ConnectionObjects) APISET().AddClass<0,BoundConnectionApi>("Binding");APISET().AddClass<0,BoundSessionsApi>("Sessions");APISET().AddClass<0,BoundRunsApi>("Runs");APISET().AddClass<0,BoundGraphsApi>("Graphs");APISET().AddClass<0,BoundModelsApi>("Models");APISET().AddClass<0,BoundCommandsApi>("Commands");APISET().AddClass<0,BoundWorkspaceApi>("Workspace");APISET().AddClass<0,BoundEventsApi>("Events");APISET().AddClass<0,BoundOperationsApi>("Operations"); END_PACKAGE};
ConnectionObjects& connection_objects(){static auto value=[] {auto owner=std::make_shared<ConnectionObjects>();retain_xmind_ipc_service(owner);(void)xmind_ipc_runtime().RegisterPackage("_xmind_connection_objects",*owner);return owner;}();return *value;}
X::Value BoundConnectionApi::Sessions(){return api_call([&]{check();(void)state->identity();return connection_objects().__xlang3_package_->CreateInstance("Sessions",std::make_shared<BoundSessionsApi>(state));});}
X::Value BoundConnectionApi::Graphs(){return api_call([&]{check();(void)state->identity();return connection_objects().__xlang3_package_->CreateInstance("Graphs",std::make_shared<BoundGraphsApi>(state));});}
X::Value BoundConnectionApi::Workspace(){return api_call([&]{check();(void)state->identity();return connection_objects().__xlang3_package_->CreateInstance("Workspace",std::make_shared<BoundWorkspaceApi>(state));});}
X::Value BoundConnectionApi::Commands(){return api_call([&]{check();(void)state->identity();return connection_objects().__xlang3_package_->CreateInstance("Commands",std::make_shared<BoundCommandsApi>(state));});}
X::Value BoundConnectionApi::Models(){return api_call([&]{check();(void)state->identity();return connection_objects().__xlang3_package_->CreateInstance("Models",std::make_shared<BoundModelsApi>(state));});}
X::Value BoundConnectionApi::Runs(){return api_call([&]{check();(void)state->identity();return connection_objects().__xlang3_package_->CreateInstance("Runs",std::make_shared<BoundRunsApi>(state));});}
X::Value BoundConnectionApi::RunEvents(std::string session,std::string run){return api_call([&]{check();return connection_objects().__xlang3_package_->CreateInstance("Events",std::make_shared<BoundEventsApi>(state->run_events(session,run)));});}
X::Value BoundConnectionApi::Events(std::string session,std::string root){return api_call([&]{check();return connection_objects().__xlang3_package_->CreateInstance("Events",std::make_shared<BoundEventsApi>(state->events(session,root)));});}
X::Value BoundConnectionApi::Operations(std::string session,std::string run){return api_call([&]{check();return connection_objects().__xlang3_package_->CreateInstance("Operations",std::make_shared<BoundOperationsApi>(state->operations(session,run)));});}
struct ConnectionsApi {
    std::shared_ptr<ApiContext> context;
    ConnectionsApi(RootApi*,std::string token);
    X::Value value(const ConnectionProfileCatalog& catalog){
        auto profiles=X::Value::List(Host());
        for(const auto& profile:catalog.profiles){const bool local=profile.kind==ConnectionKind::local;auto entry=X::Value::Dict(Host());
            set(entry,"id",X::Value::String(Host(),profile.id));set(entry,"name",X::Value::String(Host(),profile.name));set(entry,"kind",X::Value::String(Host(),local?"local":"nexus"));set(entry,"origin",X::Value::String(Host(),profile.origin));set(entry,"revision",X::Value(profile.revision));set(entry,"status",X::Value::String(Host(),local?"connected":"not_connected"));set(entry,"command_routing",X::Value(local));set(entry,"authenticated",X::Value(local));if(!profiles.Append(entry))throw Conflict("Cannot construct connection catalog");
        }
        auto result=X::Value::Dict(Host());set(result,"revision",X::Value(catalog.revision));set(result,"active_id",X::Value::String(Host(),catalog.active_id));set(result,"profiles",profiles);return result;
    }
    X::Value Open(std::string id,std::int64_t revision){return api_call([&]{auto life=context->enter();return connection_objects().__xlang3_package_->CreateInstance("Binding",std::make_shared<BoundConnectionApi>(context->bindings.open(id,revision)));});}
    X::Value List(){return api_call([&]{auto life=context->enter();auto admission=context->admit();return value(context->connections.load_or_initialize());});}
    X::Value Save(X::Value profiles,std::int64_t revision){return api_call([&]{auto life=context->enter();auto admission=context->admit();if(!profiles.IsList()||profiles.Size()>32)throw std::invalid_argument("Connection profiles require a bounded list");return value(context->connections.apply(nlohmann::json{{"profiles",sdk_values::from_value(profiles)}}.dump(),revision));});}
    X::Value Select(std::string id,std::int64_t revision){return api_call([&]{auto life=context->enter();auto admission=context->admit();return value(context->connections.select(id,revision));});}
    BEGIN_PACKAGE(ConnectionsApi)
        APISET().AddFunc<2>("Open",&ConnectionsApi::Open);
        APISET().AddFunc<0>("List",&ConnectionsApi::List);
        APISET().AddFunc<2>("Save",&ConnectionsApi::Save);
        APISET().AddFunc<2>("Select",&ConnectionsApi::Select);
    END_PACKAGE
};
struct RootApi {
    std::mutex binding;
    std::shared_ptr<ApiContext> context;
    std::shared_ptr<ApiContext> capture(){std::lock_guard lock(binding);if(!context)throw RunUnavailable("Public backend services are unavailable");return context;}
    BEGIN_PACKAGE(RootApi)
        APISET().AddClass<0,HostApi,RootApi>("Host");
        APISET().AddClass<0,PythonToolsApi>("PythonTools");
        APISET().AddClass<0,SchemasApi>("Schemas");
        APISET().AddClass<0,NativeWorkersApi>("Workers");
        APISET().AddClass<0,NativeExecutionsApi>("Executions");
        APISET().AddClass<0,NativeWorkerLinksApi>("WorkerLinks");
        APISET().AddClass<0,NativePlanningApi>("Planning");
        APISET().AddClass<1,ConnectionsApi,RootApi>("Connections");
        APISET().AddClass<1,SessionsApi,RootApi>("Sessions");
        APISET().AddClass<1,RunsApi,RootApi>("Runs");
    END_PACKAGE
};
ConnectionsApi::ConnectionsApi(RootApi* root,std::string token):context(root->capture()){context->authorize(token);}
SessionsApi::SessionsApi(RootApi* root,std::string token):context(root->capture()){context->authorize(token);}
RunsApi::RunsApi(RootApi* root,std::string token):context(root->capture()){context->authorize(token);}
RootApi& api_root(){
    static auto root=[] {auto value=std::make_shared<RootApi>();auto& runtime=xmind_ipc_runtime();retain_xmind_ipc_service(value);auto package=runtime.RegisterPackage("xmind",*value);runtime.RegisterRemoteObject("xmind",package);(void)xmind_ipc_port();return value;}();return *root;
}
}
void ensure_xmind_ipc_api(){(void)api_root();}
struct XMindIpcServer::Impl {
    RootApi& root;
    std::shared_ptr<ApiContext> context;
    std::string endpoint;
    Impl(PersistenceService& store,RunExecutor* executor,const std::string& token,BackendOwnerControl* owner):root(api_root()),context(std::make_shared<ApiContext>(store,executor,token,owner)){
        validate_local_auth_token(token);endpoint="lrpc:"+std::to_string(xmind_ipc_port());
        std::lock_guard lock(root.binding);if(root.context)throw Conflict("Public xMind services already have an active owner");root.context=context;
    }
    ~Impl(){
        {std::unique_lock lock(context->lifetime);context->active=false;}context->bindings.close();
        std::lock_guard lock(root.binding);if(root.context==context)root.context.reset();
    }
};
XMindIpcServer::XMindIpcServer(PersistenceService& store,RunExecutor* executor,const std::string& token,BackendOwnerControl* owner):impl_(std::make_unique<Impl>(store,executor,token,owner)){}
XMindIpcServer::~XMindIpcServer()=default;
LocalConnectionBindings& XMindIpcServer::connection_bindings()noexcept{return impl_->context->bindings;}
const std::string& XMindIpcServer::endpoint()const noexcept{return impl_->endpoint;}
}
