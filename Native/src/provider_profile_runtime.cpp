#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/skill_context.hpp"
#include "agentflow/gemini_provider.hpp"
#include "agentflow/provider_yaml_config.hpp"
#include <algorithm>
#include <mutex>
#include <set>
#include <random>
#include <sstream>
#include <iomanip>
#include "nlohmann/json.hpp"
namespace agentflow {
namespace {
std::string workspace_nonce(){std::random_device random;std::ostringstream value;value<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)value<<std::setw(8)<<random();return value.str();}
ProviderCatalogueFormat catalogue_format(ProviderWire wire){
    switch(wire){
        case ProviderWire::chat_completions:
        case ProviderWire::responses:return ProviderCatalogueFormat::openai;
        case ProviderWire::anthropic_messages:return ProviderCatalogueFormat::anthropic;
        case ProviderWire::gemini_generate_content:return ProviderCatalogueFormat::gemini;
    }
    throw std::invalid_argument("Invalid provider execution wire");
}
void validate_model(const ProviderProfileRoute& route,const std::string& model,bool allow_empty=false){
    if(route.wire!=ProviderWire::gemini_generate_content)return;
    // Key-only imports remain unconfigured. Every executable Gemini identity
    // goes through the same resource binder used by the native transport.
    if(allow_empty&&model.empty())return;
    (void)gemini_stream_endpoint(GeminiProviderConfig{route.endpoint,model});
}
std::string model_identity(const ProviderProfileRoute& route,const std::string& model){
    if(route.wire==ProviderWire::gemini_generate_content)return model.starts_with("models/")?model:"models/"+model;
    return model;
}
Capability model_tools(const ProviderProfileExecutionPolicy& policy,const std::string& model){
    const auto canonical=model_identity(policy.route,model);
    if(policy.model_capabilities){
        const auto found=policy.model_capabilities->models.find(canonical);
        return found==policy.model_capabilities->models.end()?Capability::unknown:found->second.tools;
    }
    if(const auto found=policy.model_tools.find(canonical);found!=policy.model_tools.end())return found->second;
    if(policy.route.wire==ProviderWire::gemini_generate_content){
        if(const auto found=policy.model_tools.find(canonical.substr(7));found!=policy.model_tools.end())return found->second;
    }
    return policy.provider.tools;
}
void validate_execution(const ProviderProfileExecutionPolicy& policy,const AgentSettings& base,const std::string& model,bool allow_empty=false){
    validate_model(policy.route,model,allow_empty);
    if(policy.model_capabilities&&!(allow_empty&&model.empty()))
        (void)bind_provider_model_policy(*policy.model_capabilities,policy.provider,model_identity(policy.route,model),base.workspace.has_value());
    if((policy.route.wire==ProviderWire::gemini_generate_content||policy.provider.chat_dialect==ChatDialect::deepseek)&&!model.empty()&&base.workspace&&model_tools(policy,model)!=Capability::supported)
        throw std::invalid_argument("Workspace agent requires declared model tool capability");
}
void validate_public_identity(const std::string& id,const std::string& model,const SecretBytes& secret){
    const auto bytes=secret.view();if(bytes.empty())return;
    if(std::search(id.begin(),id.end(),bytes.begin(),bytes.end())!=id.end()||std::search(model.begin(),model.end(),bytes.begin(),bytes.end())!=model.end())
        throw std::invalid_argument("Provider credentials are not public profile identities");
}
std::vector<ProviderProfileRoute> routes(const std::vector<ProviderProfileExecutionPolicy>& policy){
    std::vector<ProviderProfileRoute> result;
    for(const auto& value:policy){
        const auto format=catalogue_format(value.route.wire);
        if(value.provider.endpoint!=value.route.endpoint||value.provider.wire!=value.route.wire||
            !value.provider.model.empty()||value.provider.deadline.count()<=0||value.provider.idle_timeout.count()<=0)
            throw std::invalid_argument("Provider execution policy differs from profile route");
        if((value.provider.chat_dialect!=ChatDialect::openai&&value.provider.chat_dialect!=ChatDialect::deepseek)||
            (value.provider.chat_dialect==ChatDialect::deepseek&&value.route.wire!=ProviderWire::chat_completions))
            throw std::invalid_argument("Provider chat dialect differs from profile route wire");
        if(value.catalogue&&value.catalogue->format!=format)
            throw std::invalid_argument("Provider catalogue policy differs from route wire");
        if(value.context&&value.route.wire!=ProviderWire::responses)
            throw std::invalid_argument("Context strategy differs from provider route wire");
        if(value.model_capabilities){
            validate_provider_model_policy(*value.model_capabilities);
            if(value.model_capabilities->wire!=value.route.wire||!value.model_tools.empty())
                throw std::invalid_argument("Native model capabilities conflict with execution route or legacy tools policy");
        }
        // Validate the backend-owned base even before a key-only import or
        // empty registry, without selecting an executable model.
        validate_model(value.route,"xmind-policy-validation");
        if(value.model_tools.size()>256)throw std::invalid_argument("Provider model tool policy exceeds limits");
        std::set<std::string> model_ids;
        for(const auto& [model,capability]:value.model_tools){
            if(model.empty()||model.size()>256||model.starts_with("sk-")||model.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos||
                (capability!=Capability::unknown&&capability!=Capability::unsupported&&capability!=Capability::supported))
                throw std::invalid_argument("Invalid provider model tool policy");
            validate_model(value.route,model);
            if(!model_ids.insert(model_identity(value.route,model)).second)throw std::invalid_argument("Duplicate provider model tool policy");
        }
        result.push_back(value.route);
    }
    return result;
}
}
struct ProviderProfileRuntime::Impl {
    PersistenceService& store;AgentSettings base;std::vector<ProviderProfileExecutionPolicy> policy;
    std::size_t workers,capacity;ProviderProfiles profiles;ProviderProfileSnapshot state;
    std::unique_ptr<ExecutionPlatform> service;mutable std::mutex mutex;
    std::unique_ptr<WorkspaceTools> workspace_binding;std::string workspace_authority;
    Impl(PersistenceService& persistence,AgentSettings settings,std::vector<ProviderProfileExecutionPolicy> allowed,std::size_t count,std::size_t limit)
        :store(persistence),base(std::move(settings)),policy(std::move(allowed)),workers(count),capacity(limit),profiles(store,routes(policy)){
        if(count<1||count>16||limit<1||limit>4096)throw std::invalid_argument("Invalid provider execution capacity");
        if(base.workspace){workspace_binding=std::make_unique<WorkspaceTools>(*base.workspace);base.workspace=workspace_binding->root_path();workspace_authority=workspace_nonce();}
        state=profiles.snapshot();
        try{
            for(const auto& profile:state.profiles){
                const auto& allowed=route(profile.route_id);validate_model(allowed.route,profile.model,true);
                // Validate inactive streamed-text identities without applying
                // the active workspace's tool requirements to another profile.
                // Activation still goes through prepare/validate_execution.
                if(allowed.model_capabilities&&!profile.model.empty())
                    (void)bind_provider_model_policy(*allowed.model_capabilities,allowed.provider,model_identity(allowed.route,profile.model),false);
                auto owned=store.resolve_credential(allowed.route.credential_scope,profile.credential_id,allowed.route.credential_purpose).get();
                validate_public_identity(profile.id,profile.model,owned);
            }
        }catch(const std::invalid_argument&){throw DatabaseError("Stored provider profile identity or native model policy is invalid");}
        if(!state.active.empty()){
            auto owned=profiles.credential(state.active);
            const auto selected=std::find_if(state.profiles.begin(),state.profiles.end(),[&](const auto& value){return value.id==state.active;});
            try{service=prepare(*selected);}catch(const std::invalid_argument&){throw DatabaseError("Stored active provider profile is incompatible with native execution policy");}
        }else{
            auto unconfigured=base;unconfigured.provider.model.clear();unconfigured.selectable_models.clear();unconfigured.credential.reset();unconfigured.provider_identity.reset();
            service=platform(std::move(unconfigured));
        }
    }
    const ProviderProfileExecutionPolicy& route(const std::string& id)const{
        const auto found=std::find_if(policy.begin(),policy.end(),[&](const auto& value){return value.route.id==id;});
        if(found==policy.end())throw std::invalid_argument("Provider execution route is not allowed");return *found;
    }
    std::unique_ptr<ExecutionPlatform> platform(AgentSettings settings){
        auto candidate=std::make_unique<ExecutionPlatform>(store,std::move(settings),workers,capacity);
        const auto actual=candidate->execution_workspace();
        if(workspace_binding){
            if(!actual.configured||actual.workspace_id!=workspace_binding->identity())throw ToolAccessDenied("Prepared provider workspace differs from captured root");
        }else if(actual.configured)throw ToolAccessDenied("Prepared provider has an uncaptured workspace");
        return candidate;
    }
    std::unique_ptr<ExecutionPlatform> prepare(const SavedProviderProfile& profile){
        const auto& allowed=route(profile.route_id);validate_execution(allowed,base,profile.model,true);
        auto owned=store.resolve_credential(allowed.route.credential_scope,profile.credential_id,allowed.route.credential_purpose).get();
        validate_public_identity(profile.id,profile.model,owned);
        if(profile.model.empty()){
            auto settings=base;settings.provider.model.clear();settings.selectable_models.clear();settings.credential.reset();settings.provider_identity.reset();
            return platform(std::move(settings));
        }
        auto settings=base;settings.provider=allowed.provider;settings.provider.model=profile.model;
        if(allowed.model_capabilities)settings.provider=bind_provider_model_policy(*allowed.model_capabilities,settings.provider,model_identity(allowed.route,profile.model),base.workspace.has_value());
        if(settings.provider.wire==ProviderWire::anthropic_messages&&!settings.max_output_tokens)settings.max_output_tokens=4096;
        settings.context=allowed.context;
        settings.provider.tools=model_tools(allowed,profile.model);
        settings.selectable_models.clear();settings.credential=CredentialReference{allowed.route.credential_scope,profile.credential_id,allowed.route.credential_purpose};
        settings.provider_identity=ProviderExecutionIdentity{profile.id,profile.route_id,allowed.route.provider,profile.revision};
        if(settings.context){
            for(auto& [model,capacity]:settings.context->model_capacities){
                if(capacity.model_id!=model||capacity.wire!="responses")throw std::invalid_argument("Context capacity model binding differs");
                capacity.provider_identity_json=provider_context_json(settings,model);
                auto destination=settings.provider;destination.model=model;
                capacity.route_identity=context_route_identity(destination,capacity.provider_identity_json);
            }
            const auto found=settings.context->model_capacities.find(profile.model);
            if(!settings.max_output_tokens&&found!=settings.context->model_capacities.end()&&found->second.verified&&found->second.output_tokens)
                settings.max_output_tokens=*found->second.output_tokens;
        }
        return platform(std::move(settings));
    }
    ExecutionWorkspaceMetadata workspace_metadata()const{
        if(!workspace_binding)return {};return {true,workspace_binding->root_path(),workspace_binding->identity(),workspace_authority};
    }
    void mutable_state(std::int64_t expected)const{
        if(expected<0||expected!=state.revision)throw Conflict("Provider profile runtime revision changed");
        if(!service->healthy())throw RunUnavailable("Reconcile execution faults before changing provider profiles");
        if(!service->idle())throw Conflict("Wait for active runs before changing provider profiles");
    }
    void admission(const ProviderProfileAdmission& expected)const{
        if(expected.revision<0||expected.revision>9007199254740991||expected.revision!=state.revision||expected.id!=state.active)throw Conflict("Provider profile changed before run admission");
    }
    ProviderProfileRuntimeMetadata metadata()const{
        ProviderProfileRuntimeMetadata result{state.revision,state.active,{}};
        for(const auto& value:state.profiles)result.profiles.push_back({value.id,value.route_id,route(value.route_id).route.provider,value.model,value.revision});
        return result;
    }
};
ProviderProfileRuntime::ProviderProfileRuntime(PersistenceService& store,AgentSettings base,std::vector<ProviderProfileExecutionPolicy> policy,std::size_t workers,std::size_t capacity)
    :impl_(std::make_unique<Impl>(store,std::move(base),std::move(policy),workers,capacity)){}
ProviderProfileRuntime::~ProviderProfileRuntime()=default;
ProviderProfileRuntimeMetadata ProviderProfileRuntime::configuration()const{std::lock_guard lock(impl_->mutex);return impl_->metadata();}
std::vector<ProviderProfileRouteMetadata> ProviderProfileRuntime::profile_routes()const{
    std::vector<ProviderProfileRouteMetadata> result;
    for(const auto& policy:impl_->policy)result.push_back({policy.route.id,policy.route.provider,policy.route.wire,policy.catalogue.has_value()});
    return result;
}
std::vector<std::string> ProviderProfileRuntime::discover_models(std::string id,std::string route_id,SecretBytes key,std::int64_t expected,std::stop_token cancel){
    if(id.empty()||id.size()>256||id.starts_with("sk-")||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)throw std::invalid_argument("Invalid provider profile identity");
    ProviderCataloguePolicy catalogue;
    {
        std::lock_guard lock(impl_->mutex);
        if(expected<0||expected!=impl_->state.revision||impl_->profiles.snapshot().revision!=expected)throw Conflict("Provider profile discovery revision changed");
        const auto& allowed=impl_->route(route_id);if(!allowed.catalogue)throw std::invalid_argument("Provider model discovery is unavailable for this route");catalogue=*allowed.catalogue;
        const auto saved=std::find_if(impl_->state.profiles.begin(),impl_->state.profiles.end(),[&](const auto& profile){return profile.id==id;});
        if(saved!=impl_->state.profiles.end()&&impl_->route(saved->route_id).route.provider!=allowed.route.provider)throw std::invalid_argument("A profile cannot discover another provider family");
        if(key.view().empty()){
            if(saved==impl_->state.profiles.end())throw std::invalid_argument("New provider profile discovery requires a key");
            if(saved->route_id!=route_id)throw std::invalid_argument("Saved-key discovery must use the profile's own route");
            key=impl_->store.resolve_credential(allowed.route.credential_scope,saved->credential_id,allowed.route.credential_purpose).get();
        }
        validate_public_identity(id,"",key);
    }
    auto result=discover_provider_models(catalogue,key,cancel);
    {
        std::lock_guard lock(impl_->mutex);if(expected!=impl_->state.revision||impl_->profiles.snapshot().revision!=expected)throw Conflict("Provider profile changed during model discovery");
        const auto& allowed=impl_->route(route_id);
        if(allowed.model_capabilities)
            std::erase_if(result,[&](const auto& model){try{validate_execution(allowed,impl_->base,model);return false;}catch(const std::invalid_argument&){return true;}});
        if(impl_->base.workspace&&(allowed.route.wire==ProviderWire::gemini_generate_content||allowed.provider.chat_dialect==ChatDialect::deepseek))
            std::erase_if(result,[&](const auto& model){return model_tools(allowed,model)!=Capability::supported;});
    }
    return result;
}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::save_profile(std::string id,std::string route,std::string model,SecretBytes key,std::int64_t expected,bool activate){
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(expected);
    // ProviderProfiles encrypts a candidate before its validation callback.
    // Reject undeclared model tools, invalid resources and reflected identities before
    // even candidate persistence for any provider family.
    const auto& allowed=impl_->route(route);validate_execution(allowed,impl_->base,model);
    if(!key.view().empty())validate_public_identity(id,model,key);
    else{
        const auto saved=std::find_if(impl_->state.profiles.begin(),impl_->state.profiles.end(),[&](const auto& profile){return profile.id==id;});
        if(saved!=impl_->state.profiles.end()){
            const auto& old=impl_->route(saved->route_id).route;
            auto owned=impl_->store.resolve_credential(old.credential_scope,saved->credential_id,old.credential_purpose).get();
            validate_public_identity(id,model,owned);
        }
    }
    const bool replace=activate||impl_->state.active==id;std::unique_ptr<ExecutionPlatform> candidate;
    auto next=impl_->profiles.save(std::move(id),std::move(route),std::move(model),std::move(key),expected,activate,
        [&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    impl_->state=std::move(next);std::unique_ptr<ExecutionPlatform> previous;
    if(replace){previous=std::move(impl_->service);impl_->service=std::move(candidate);}
    const auto result=impl_->metadata();lock.unlock();previous.reset();candidate.reset();return result;
}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::select_profile(std::string id,std::int64_t expected){
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(expected);std::unique_ptr<ExecutionPlatform> candidate;
    auto next=impl_->profiles.select(std::move(id),expected,[&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    impl_->state=std::move(next);auto previous=std::move(impl_->service);impl_->service=std::move(candidate);
    const auto result=impl_->metadata();lock.unlock();previous.reset();return result;
}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::import_yaml_configuration(const std::filesystem::path& path,std::int64_t expected){
    std::vector<std::string> allowed_routes;
    for(const auto& policy:impl_->policy)allowed_routes.push_back(policy.route.id);
    // The reader owns/wipes the bounded plaintext input. No view/API handler
    // can supply this local path or obtain the decoded secret-bearing values.
    auto configuration=read_provider_yaml_config(path,allowed_routes);
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(expected);
    if(impl_->profiles.snapshot().revision!=expected)throw Conflict("Provider YAML registry revision changed");
    std::erase_if(configuration.profiles,[&](const auto& entry){
        return entry.key.view().empty()&&std::none_of(impl_->state.profiles.begin(),impl_->state.profiles.end(),
            [&](const auto& saved){return saved.id==entry.id;});
    });
    // Empty template placeholders do not create fake credentials/models or
    // replace the running provider. An explicit selection still must exist.
    if(configuration.profiles.empty()&&!configuration.active_profile)return impl_->metadata();
    for(const auto& entry:configuration.profiles){
        const auto& allowed=impl_->route(entry.route_id);
        const auto saved=std::find_if(impl_->state.profiles.begin(),impl_->state.profiles.end(),
            [&](const auto& value){return value.id==entry.id;});
        const auto model=entry.model?*entry.model:saved!=impl_->state.profiles.end()?saved->model:std::string{};
        validate_execution(allowed,impl_->base,model,true);
        if(!entry.key.view().empty())validate_public_identity(entry.id,model,entry.key);
        else if(saved!=impl_->state.profiles.end()){
            const auto& previous=impl_->route(saved->route_id).route;
            auto owned=impl_->store.resolve_credential(previous.credential_scope,saved->credential_id,previous.credential_purpose).get();
            validate_public_identity(entry.id,model,owned);
        }
    }
    std::unique_ptr<ExecutionPlatform> candidate;
    auto next=impl_->profiles.save_config(std::move(configuration.profiles),std::move(configuration.active_profile),expected,
        [&](const auto& profile,const auto& allowed){
            validate_execution(impl_->route(allowed.id),impl_->base,profile.model,true);
            auto owned=impl_->store.resolve_credential(allowed.credential_scope,profile.credential_id,allowed.credential_purpose).get();
            validate_public_identity(profile.id,profile.model,owned);
        },[&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    // Publication succeeded. Adopt exactly that prepared generation only when
    // there is an active selection; inactive imports retain the current service.
    impl_->state=std::move(next);std::unique_ptr<ExecutionPlatform> previous;
    if(candidate){previous=std::move(impl_->service);impl_->service=std::move(candidate);}
    const auto result=impl_->metadata();lock.unlock();previous.reset();return result;
}
ProviderProfileRuntimeMetadata ProviderProfileRuntime::import_existing_profile(std::string id,std::string route,std::string model,std::string credential,std::int64_t revision){
    std::unique_lock lock(impl_->mutex);impl_->mutable_state(0);std::unique_ptr<ExecutionPlatform> candidate;
    const auto& allowed=impl_->route(route);validate_execution(allowed,impl_->base,model,true);
    auto owned=impl_->store.resolve_credential(allowed.route.credential_scope,credential,allowed.route.credential_purpose).get();
    validate_public_identity(id,model,owned);
    auto next=impl_->profiles.import_existing(std::move(id),std::move(route),std::move(model),std::move(credential),revision,
        [&](const auto& profile,const auto&){candidate=impl_->prepare(profile);});
    impl_->state=std::move(next);auto previous=std::move(impl_->service);impl_->service=std::move(candidate);
    const auto result=impl_->metadata();lock.unlock();previous.reset();return result;
}
bool ProviderProfileRuntime::import_legacy_configuration(std::string id){
    {std::lock_guard lock(impl_->mutex);if(impl_->state.revision!=0)return false;}
    std::string source;
    try{source=impl_->store.information("native-provider","active").get();}catch(const NotFound&){return false;}
    std::string route_id,model,credential;std::int64_t revision=0;
    try{
        using Json=nlohmann::json;
        if(source.size()>65536)throw DatabaseError("Legacy provider configuration exceeds limits");
        std::vector<std::set<std::string>> fields;
        const auto record=Json::parse(source,[&](int depth,Json::parse_event_t event,Json& value){
            if(depth>16)throw DatabaseError("Invalid legacy provider configuration");
            if(event==Json::parse_event_t::object_start)fields.emplace_back();
            else if(event==Json::parse_event_t::object_end)fields.pop_back();
            else if(event==Json::parse_event_t::key&&!fields.back().insert(value.get<std::string>()).second)throw DatabaseError("Duplicate legacy provider field");
            return true;
        });
        if(!record.is_object()||(record.size()!=5&&record.size()!=6)||record.at("provider")!="openai"||
            !record.at("revision").is_number_integer()|| (record.size()==6&&!record.contains("wire")))throw DatabaseError("Invalid legacy provider configuration");
        revision=record.at("revision").get<std::int64_t>();
        if(revision<1||revision>9007199254740991)throw DatabaseError("Invalid legacy provider revision");
        const auto endpoint=record.at("endpoint").get<std::string>();
        model=record.at("model").get<std::string>();credential=record.at("credential_id").get<std::string>();
        for(const auto& allowed:impl_->policy){
            const auto& route=allowed.route;
            if(route.provider!="openai"||route.credential_scope!="server"||route.endpoint!=endpoint)continue;
            if(route.wire!=ProviderWire::chat_completions&&route.wire!=ProviderWire::responses)continue;
            const auto wire=route.wire==ProviderWire::responses?"responses":"chat-completions";
            if(record.contains("wire")&&record.at("wire")!=wire)continue;
            if(!route_id.empty())throw DatabaseError("Ambiguous legacy provider route");
            route_id=route.id;
        }
        if(route_id.empty())throw DatabaseError("Legacy provider configuration differs from backend policy");
    }catch(const DatabaseError&){throw;}catch(...){throw DatabaseError("Legacy provider configuration is invalid");}
    // Import validates identity, resolves the exact owned encrypted credential,
    // prepares execution and publishes with absent-registry CAS. No rotation.
    const auto& route=impl_->route(route_id).route;
    auto owned=impl_->store.resolve_credential(route.credential_scope,credential,route.credential_purpose).get();
    const auto bytes=owned.view();
    if(model.starts_with("sk-")||(model.size()==bytes.size()&&std::equal(model.begin(),model.end(),bytes.begin(),[](char left,std::uint8_t right){return static_cast<unsigned char>(left)==right;})))model.clear();
    import_existing_profile(std::move(id),std::move(route_id),std::move(model),std::move(credential),revision);
    return true;
}
Run ProviderProfileRuntime::submit(std::string id,std::string session,std::string prompt){return submit_model(std::move(id),std::move(session),std::move(prompt),{});}
Run ProviderProfileRuntime::submit_model(std::string id,std::string session,std::string prompt,std::string model){std::lock_guard lock(impl_->mutex);return impl_->service->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));}
Run ProviderProfileRuntime::submit_profile(std::string id,std::string session,std::string prompt,std::string model,ProviderProfileAdmission expected){std::lock_guard lock(impl_->mutex);impl_->admission(expected);return impl_->service->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));}
Run ProviderProfileRuntime::submit_graph_profile(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,ProviderProfileAdmission expected){std::lock_guard lock(impl_->mutex);impl_->admission(expected);return impl_->service->submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));}
Run ProviderProfileRuntime::submit_message(std::string id,std::string context,std::string message,std::string content,std::string identity){std::lock_guard lock(impl_->mutex);return impl_->service->submit_message(std::move(id),std::move(context),std::move(message),std::move(content),std::move(identity));}
ExecutionWorkspaceMetadata ProviderProfileRuntime::execution_workspace()const{std::lock_guard lock(impl_->mutex);return impl_->workspace_metadata();}
bool ProviderProfileRuntime::supports_skill_catalogue()const{std::lock_guard lock(impl_->mutex);return static_cast<bool>(impl_->workspace_binding);}
WorkspaceSkillCatalogue ProviderProfileRuntime::workspace_skills()const{
    std::lock_guard lock(impl_->mutex);if(!impl_->workspace_binding)throw RunUnavailable("Workspace skill inspection requires an opened root");
    const auto workspace=impl_->workspace_metadata();const auto catalogue=SkillContext(*impl_->workspace_binding).catalogue_json();
    validate_workspace_admission({workspace.workspace_id,workspace.authority_id},impl_->workspace_metadata());return {workspace,catalogue};
}
WorkspaceSessionSkills ProviderProfileRuntime::session_skills(const std::string& session)const{
    std::lock_guard lock(impl_->mutex);if(!impl_->workspace_binding)throw RunUnavailable("Session skills require an opened root");const auto workspace=impl_->workspace_metadata();return {workspace,impl_->store.session_skills(session,workspace.workspace_id).get()};
}
WorkspaceSessionSkills ProviderProfileRuntime::replace_session_skills(const std::string& session,std::vector<std::string> ids,std::int64_t revision,WorkspaceAdmission expected){
    std::lock_guard lock(impl_->mutex);if(!impl_->workspace_binding||!impl_->service->healthy())throw RunUnavailable("Session skill controls are unavailable");const auto workspace=impl_->workspace_metadata();validate_workspace_admission(expected,workspace);
    SkillSelections selections{workspace.workspace_id,ids,ids};if(!ids.empty()){SkillContext validation(*impl_->workspace_binding);validation.restore(selections);validation.prepare();validation.precondition().verify({});}
    validate_workspace_admission(expected,impl_->workspace_metadata());return {workspace,impl_->store.replace_session_skills(session,std::move(selections),revision).get()};
}
Run ProviderProfileRuntime::submit_workspace(std::string id,std::string session,std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile){
    std::lock_guard lock(impl_->mutex);validate_workspace_admission(expected,impl_->workspace_metadata());if(profile)impl_->admission(*profile);
    return impl_->service->submit_model(std::move(id),std::move(session),std::move(prompt),std::move(model));
}
Run ProviderProfileRuntime::submit_graph_workspace(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model,WorkspaceAdmission expected,std::optional<ProviderProfileAdmission> profile){
    std::lock_guard lock(impl_->mutex);validate_workspace_admission(expected,impl_->workspace_metadata());if(profile)impl_->admission(*profile);
    return impl_->service->submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));
}
std::vector<std::string> ProviderProfileRuntime::models()const{std::lock_guard lock(impl_->mutex);return impl_->service->models();}
void ProviderProfileRuntime::cancel(const std::string& id){std::lock_guard lock(impl_->mutex);impl_->service->cancel(id);}
bool ProviderProfileRuntime::healthy()const{std::lock_guard lock(impl_->mutex);return impl_->service->healthy();}
bool ProviderProfileRuntime::supports_delegation()const{std::lock_guard lock(impl_->mutex);return impl_->service->supports_delegation();}
bool ProviderProfileRuntime::supports_dynamic_planning()const{std::lock_guard lock(impl_->mutex);return impl_->service->supports_dynamic_planning();}
bool ProviderProfileRuntime::supports_context()const{std::lock_guard lock(impl_->mutex);return impl_->service->supports_context();}
ContextControlSnapshot ProviderProfileRuntime::context_status(const std::string& session,const std::string& model)const{
    std::lock_guard lock(impl_->mutex);return impl_->service->context_status(session,model);
}
ContextManualStatus ProviderProfileRuntime::context_request(const std::string& session,const std::string& request,const std::string& model)const{
    std::lock_guard lock(impl_->mutex);return impl_->service->context_request(session,request,model);
}
ContextManualStatus ProviderProfileRuntime::request_context(const std::string& session,const std::string& request,const std::string& actor,std::int64_t revision,const std::string& model){
    std::lock_guard lock(impl_->mutex);return impl_->service->request_context(session,request,actor,revision,model);
}
ContextManualStatus ProviderProfileRuntime::request_context_profile(const std::string& session,const std::string& request,const std::string& actor,std::int64_t revision,const std::string& model,ProviderProfileAdmission expected){
    std::lock_guard lock(impl_->mutex);impl_->admission(expected);return impl_->service->request_context(session,request,actor,revision,model);
}
Run ProviderProfileRuntime::plan_input(const std::string& root,const std::string& request,std::string input,const std::string& actor,std::int64_t revision,std::int64_t sequence){std::lock_guard lock(impl_->mutex);return impl_->service->plan_input(root,request,std::move(input),actor,revision,sequence);}
Run ProviderProfileRuntime::resume_plan(const std::string& root,const std::string& actor,std::int64_t revision,std::int64_t sequence){std::lock_guard lock(impl_->mutex);return impl_->service->resume_plan(root,actor,revision,sequence);}
bool ProviderProfileRuntime::available()const{std::lock_guard lock(impl_->mutex);return impl_->service->available();}
std::vector<GraphExecutionMetadata> ProviderProfileRuntime::graphs()const{std::lock_guard lock(impl_->mutex);return impl_->service->graphs();}
Run ProviderProfileRuntime::submit_graph(std::string id,std::string session,std::string graph,std::int64_t revision,std::string prompt,std::string model){std::lock_guard lock(impl_->mutex);return impl_->service->submit_graph(std::move(id),std::move(session),std::move(graph),revision,std::move(prompt),std::move(model));}
GraphRootRecord ProviderProfileRuntime::human_input(const std::string& root,const std::string& node,const std::string& input,const std::string& actor,std::int64_t revision){std::lock_guard lock(impl_->mutex);return impl_->service->human_input(root,node,input,actor,revision);}
Run ProviderProfileRuntime::resume_graph(const std::string& root,const std::string& actor,std::int64_t revision){std::lock_guard lock(impl_->mutex);return impl_->service->resume_graph(root,actor,revision);}
GraphContextMetadata ProviderProfileRuntime::graph_context(const std::string& root)const{std::lock_guard lock(impl_->mutex);return impl_->service->graph_context(root);}
}
