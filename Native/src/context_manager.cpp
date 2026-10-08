#include "agentflow/context_manager.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <type_traits>
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
std::string identity(){std::random_device random;std::ostringstream out;out<<std::hex<<std::setfill('0');for(int i=0;i<4;++i)out<<std::setw(8)<<random();return out.str();}
std::int64_t elapsed(std::chrono::steady_clock::time_point start){return std::chrono::ceil<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
ContextFailureCode failure_code(const std::exception_ptr& failure){
    try{std::rethrow_exception(failure);}
    catch(const TransportCancelled&){return ContextFailureCode::cancelled;}
    catch(const TransportTimeout&){return ContextFailureCode::deadline;}
    catch(const RootBudgetDeadlineExceeded&){return ContextFailureCode::deadline;}
    catch(const ContextCapacityExceeded&){return ContextFailureCode::capacity_exceeded;}
    catch(const ModelRequestCapacityExceeded&){return ContextFailureCode::capacity_exceeded;}
    catch(const ContextSourceChanged&){return ContextFailureCode::source_changed;}
    catch(const ContextBindingChanged&){return ContextFailureCode::binding_changed;}
    catch(const ResponsesContextError&){return ContextFailureCode::invalid_result;}
    catch(const ProviderHttpError&){return ContextFailureCode::provider_failure;}
    catch(const ModelProtocolError&){return ContextFailureCode::invalid_result;}
    catch(const TransportError&){return ContextFailureCode::provider_failure;}
    catch(...){return ContextFailureCode::native_failure;}
}
struct EngineResult {
    ModelRequest request;
    ContextSnapshot snapshot;
    std::optional<ModelCallReservation> inference;
    std::string step_id;
    std::optional<ContextInputMeasure> measure;
    std::optional<ContextProjection> projection;
};
class Preparation {
    PersistenceService& store;
    ChatProviderConfig provider;
    const ContextRuntimePolicy& policy;
    const ContextManager::MessageDecoder& decode;
    const ContextOwner& owner;
    RootExecutionBudget* budget;
    const SecretBytes* bearer;
    std::stop_token cancel;
    std::chrono::steady_clock::time_point deadline;
    ContextReadBound bound;
    std::optional<std::string> compaction_id;
    std::optional<ContextStepReservation> funded;
    std::optional<ModelCallReservation> held;
    std::string owned_step_id,owned_attempt_id;
    std::optional<VerifiedContextCapacity> capacity;
    std::optional<std::chrono::steady_clock::time_point> maintenance_started;
    std::chrono::steady_clock::time_point preparation_started=std::chrono::steady_clock::now();

    void check()const{
        if(cancel.stop_requested())throw TransportCancelled("Context preparation cancelled");
        if(budget)budget->check(cancel);
        if(std::chrono::steady_clock::now()>=deadline)throw RootBudgetDeadlineExceeded("Context preparation deadline elapsed");
    }
    template<class Refresh,class Admit>
    auto admit_with_current_budget(Refresh&& refresh,Admit&& admit){
        // Concurrent leaves may charge the shared root between its read and
        // this admission. Only a rolled-back budget revision conflict permits
        // refreshing metadata: all payload/member/source identities stay fixed.
        // Counts, accepted maintenance and provider requests are never replayed.
        constexpr unsigned max_admission_attempts=16;
        for(unsigned attempt=0;attempt<max_admission_attempts;++attempt){
            check();if(budget)refresh(budget->snapshot().revision);
            try{check();return admit();}
            catch(const ContextBudgetChanged&){
                if(!budget||attempt+1==max_admission_attempts)throw;
            }
        }
        throw ContextBudgetChanged("Context budget admission remained busy");
    }
    ChatProviderConfig transport_config()const{
        check();auto value=provider;
        const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
        value.deadline=std::min({value.deadline,remaining,std::chrono::milliseconds(policy.compaction.maintenance_deadline_ms)});
        return value;
    }
    ModelRequest project(const ContextSnapshot& snapshot,const ModelRequest& trusted,
        std::int64_t covered,const std::optional<ResponsesCanonicalWindow>& window){
        check();auto request=trusted;request.canonical_window=window;
        std::vector<std::int64_t> ordinals;std::size_t messages=request.messages.size(),bytes=0;
        for(const auto& group:snapshot.groups)if(group.ordinal>covered){
            if(group.message_count<1||group.payload_bytes<1||
               static_cast<std::uint64_t>(group.message_count)>policy.compaction.max_messages-messages||
               static_cast<std::uint64_t>(group.payload_bytes)>bound.max_payload_bytes-bytes)
                throw ContextCapacityExceeded("Original context tail exceeds its native bound");
            messages+=static_cast<std::size_t>(group.message_count);bytes+=static_cast<std::size_t>(group.payload_bytes);ordinals.push_back(group.ordinal);
        }
        if(!ordinals.empty())for(const auto& payload:store.context_group_payloads(snapshot,ordinals,bound).get())
            for(const auto& original:payload.originals)request.messages.push_back(decode(original));
        return request;
    }
    ContextInputMeasure count(const ContextSnapshot& snapshot,const ModelRequest& request){
        check();const auto selected=transport_config();
        const auto inference_json=serialize_responses_request(selected,request);
        const auto count_json=serialize_responses_input_tokens_request(selected,request);
        ContextMeasureRequestSpec spec;spec.id=identity();spec.owner_id=owner.owner_run_id;
        if(budget)spec.root_run_id=owner.root_run_id;
        spec.compaction_id=compaction_id;spec.scope=owner.scope;spec.binding=owner.binding;
        spec.head_revision=snapshot.head.revision;spec.source_watermark=snapshot.source_watermark;
        spec.model_id=provider.model;spec.payload_binding=context_digest(inference_json);
        spec.serialized_bytes=static_cast<std::int64_t>(inference_json.size());
        spec.max_requests=policy.compaction.max_count_requests;spec.deadline_ms=selected.deadline.count();
        spec.input_json=count_json;spec.count_request_binding=context_digest(count_json);
        store.begin_context_measure(spec).get();
        const auto started=std::chrono::steady_clock::now();
        try{
            check();store.start_context_measure(spec.id).get();
            const auto actual=count_responses_context(selected,request,bearer,cancel);
            ContextInputMeasure measure;measure.id=spec.id;measure.owner_id=spec.owner_id;
            measure.scope=spec.scope;measure.binding=spec.binding;measure.head_revision=spec.head_revision;
            measure.source_watermark=spec.source_watermark;measure.payload_binding=spec.payload_binding;
            measure.model_id=spec.model_id;measure.semantics=ContextMeasureSemantics::provider_exact;
            measure.value=actual.input_tokens;measure.serialized_bytes=spec.serialized_bytes;
            measure.measured_elapsed_ms=elapsed(started);measure.actual_response_json=actual.response_json;
            try{store.finish_context_measure(measure).get();}
            catch(...){throw ContextOutcomeUnrecorded("Acknowledged context count could not be recorded");}
            return measure;
        }catch(const ContextOutcomeUnrecorded&){throw;}
         catch(...){
            const auto failure=std::current_exception();
            try{store.retire_context_measure(spec.id,failure_code(failure),elapsed(started)).get();}
            catch(...){throw ContextOutcomeUnrecorded("Context count retirement could not be recorded");}
            std::rethrow_exception(failure);
        }
    }
    ContextCapacityEvaluation evaluate(const ContextSnapshot& snapshot,const ModelRequest& request,
        const std::optional<ContextInputMeasure>& measure){
        const auto encoded=serialize_responses_request(provider,request);
        ContextCapacityRequest spec;spec.scope=owner.scope;spec.binding=owner.binding;
        spec.head_revision=snapshot.head.revision;spec.source_watermark=snapshot.source_watermark;
        spec.payload_binding=context_digest(encoded);spec.wire="responses";spec.model_id=provider.model;
        spec.route_identity=context_route_identity(provider,owner.binding.provider_identity_json);
        spec.measure=measure;spec.requested_output_tokens=request.max_output_tokens;
        spec.buffer_tokens=policy.buffer_tokens;spec.serialized_bytes=static_cast<std::int64_t>(encoded.size());
        spec.max_serialized_bytes=static_cast<std::int64_t>(policy.compaction.max_request_bytes);
        spec.automatic_enabled=policy.compaction.automatic;
        if(capacity){spec.capacity_revision=capacity->revision;spec.capacity_source_binding=capacity->source_binding;}
        return evaluate_context_capacity(spec,capacity);
    }
    void retire(const ContextCompactionFailure& failure){
        try{store.retire_context_compaction(failure).get();}
        catch(...){throw ContextOutcomeUnrecorded("Context maintenance retirement could not be recorded");}
    }
    void retire_undispatched_inference(){
        const auto member=funded&&funded->inference?funded->inference:held;
        if(!member&&owned_attempt_id.empty())return;
        const auto attempt=member?member->attempt_id:owned_attempt_id;
        try{
            std::optional<InferenceStepRecord> step;
            try{step=store.inference_step(owned_step_id).get();}
            catch(const NotFound&){}
            if(step){
                const auto actual=std::find_if(step->physical_attempts.begin(),step->physical_attempts.end(),
                    [&](const auto& value){return value.attempt_id==attempt;});
                if(step->root_run_id!=owner.root_run_id||step->owner_run_id!=owner.owner_run_id||
                   actual==step->physical_attempts.end())
                    throw ContextOutcomeUnrecorded("Preparation lost its exact inference reservation");
                if(actual->state=="finished"||actual->state=="interrupted")return;
                if(actual->state!="reserved")throw ContextOutcomeUnrecorded("Preparation cannot retire dispatched inference");
                store.finish_inference_attempt({step->id,actual->attempt_id,
                    cancel.stop_requested()?InferenceAttemptOutcome::cancelled:InferenceAttemptOutcome::failed,
                    {},"",0}).get();
            }else{
                if(!member)return; // The proposed admission was rolled back.
                // A signed continuation can be reserved before counting but
                // have no typed step yet. Retire that original unstarted member
                // without refunding it or changing its signed call pointer.
                if(!held||held->attempt_id!=member->attempt_id||held->state!="reserved")
                    throw ContextOutcomeUnrecorded("Preparation has no original reserved continuation");
                budget->finish(*held);
            }
        }catch(const ContextOutcomeUnrecorded&){throw;}
         catch(...){throw ContextOutcomeUnrecorded("Undispatched inference retirement could not be recorded");}
    }
public:
    Preparation(PersistenceService& persistence,ChatProviderConfig configured,const ContextRuntimePolicy& configured_policy,
        const ContextManager::MessageDecoder& decoder,const ContextOwner& context_owner,RootExecutionBudget* root,
        const SecretBytes* credential,std::stop_token stopped,
        std::optional<std::chrono::steady_clock::time_point> owner_deadline={})
        :store(persistence),provider(std::move(configured)),policy(configured_policy),decode(decoder),owner(context_owner),
         budget(root),bearer(credential),cancel(stopped),deadline(std::chrono::steady_clock::now()+std::chrono::milliseconds(configured_policy.compaction.maintenance_deadline_ms)){
        if(root)deadline=std::min(deadline,root->deadline());
        if(owner_deadline)deadline=std::min(deadline,*owner_deadline);
        bound.max_groups=policy.compaction.max_groups;bound.max_payload_bytes=policy.compaction.max_request_bytes;
        const auto found=policy.model_capacities.find(provider.model);
        if(found!=policy.model_capacities.end()){
            const auto& record=found->second;
            if(record.verified&&record.provider_identity_json==owner.binding.provider_identity_json&&
               record.route_identity==context_route_identity(provider,owner.binding.provider_identity_json)&&
               record.wire=="responses"&&record.model_id==provider.model&&record.revision>0&&
               record.source_binding.size()==64&&record.source_binding.find_first_not_of("0123456789abcdef")==std::string::npos)
                capacity=record;
        }
    }
    EngineResult run(const ModelRequest& trusted,const PreparedModelContext* failed=nullptr) try {
        check();if(trusted.canonical_window)throw std::invalid_argument("Trusted context must not contain a provider window");
        for(const auto& message:trusted.messages)if(message.role!=MessageRole::system&&message.role!=MessageRole::developer)
            throw std::invalid_argument("Trusted context must contain only current instructions");
        if(trusted.messages.size()>policy.compaction.max_messages)throw ContextCapacityExceeded("Trusted context exceeds limits");
        if(failed&&(!budget||!policy.compaction.automatic))throw ContextUnavailable("Automatic context rebuild is unavailable");
        if(budget){
            if(owner.root_run_id!=budget->root_id()||owner.owner_run_id.empty())throw ContextBindingChanged("Context owner differs from its root budget");
            const auto run=store.run(owner.owner_run_id).get();
            if(run.state!=RunState::running||run.graph_root||
               (owner.scope.kind==ContextScopeKind::session&&(owner.scope.id!=run.session_id||!run.parent_id.empty()||owner.role!=ModelCallRole::parent||owner.root_run_id!=run.id))||
               (owner.scope.kind==ContextScopeKind::execution&&(owner.scope.id!=run.id||run.parent_id.empty()||owner.role!=ModelCallRole::leaf))||
               (owner.planning_call_id&&(!run.parent_id.empty()||owner.root_run_id!=run.id)))
                throw ContextBindingChanged("Context scope differs from its actual execution owner");
        }else if(!owner.root_run_id.empty()||owner.planning_call_id||owner.scope.kind!=ContextScopeKind::session||owner.owner_run_id.empty())
            throw ContextBindingChanged("Idle context requires its exclusive session maintenance owner");
        if(failed){
            const auto step=store.inference_step(failed->inference_step_id).get();
            const auto member=std::find_if(step.physical_attempts.begin(),step.physical_attempts.end(),[&](const auto& value){return value.attempt_id==failed->inference.attempt_id;});
            if(step.root_run_id!=owner.root_run_id||step.owner_run_id!=owner.owner_run_id||step.scope!=owner.scope||
               step.binding!=owner.binding||step.planning_call_id!=owner.planning_call_id||step.state!="context_overflow"||
               step.rebuild_count!=0||step.successful_attempt_id||member==step.physical_attempts.end()||member->state!="finished")
                throw ContextBindingChanged("Context rebuild differs from its actual failed inference step");
        }
        EngineResult result;result.snapshot=store.context_snapshot(owner.scope,owner.binding,bound).get();
        const auto old=store.context_projection(owner.scope,owner.binding,bound).get();
        std::optional<ResponsesCanonicalWindow> window;
        if(old){
            if(old->kind!=ContextProjectionKind::responses_canonical||old->head.revision!=result.snapshot.head.revision||
               old->head.checkpoint_id!=result.snapshot.head.checkpoint_id)
                throw ContextUnavailable("Context checkpoint is incompatible with its native head");
            window=parse_responses_canonical_window(old->projection_json);
        }
        result.request=project(result.snapshot,trusted,result.snapshot.head.covered_through_ordinal,window);
        const auto manual=result.snapshot.pending_manual_request;
        const auto selection=select_context(result.snapshot,policy.compaction);
        // An idle owner must claim the session before even a read-only counting
        // request. It has no fictional prompt or funded inference.
        const bool idle=!budget;
        if(idle&&!manual)throw ContextUnavailable("Idle maintenance requires its authenticated pending request");
        if(idle&&manual->owner_id&&*manual->owner_id!=owner.owner_run_id)
            throw ContextBindingChanged("Idle maintenance cannot adopt another request owner");
        if((idle||failed||manual)&&!selection){
            // A service-preclaimed idle lease has its own typed retirement.
            // Generic request retirement cannot erase that actual owner.
            if(manual&&!manual->owner_id)try{
                store.retire_context_request(manual->id,owner.binding,ContextFailureCode::capacity_exceeded).get();
            }catch(...){throw ContextOutcomeUnrecorded("Manual context request retirement could not be recorded");}
            throw ContextCapacityExceeded("There is no complete reclaimable context prefix");
        }
        if(budget&&owner.planning_call_id&&!failed)held=budget->reserve_continuation(*owner.planning_call_id,cancel);
        std::string step_id=failed?failed->inference_step_id:(held?
            (held->inference_step_id.empty()?context_digest("native.context.held-step.v1:"+owner.root_run_id+":"+*owner.planning_call_id):held->inference_step_id):identity());
        owned_step_id=step_id;
        if(held){owned_attempt_id=held->attempt_id;if(held->state!="reserved")throw ContextUnavailable("Signed continuation is already dispatched or retired");}
        auto admit=[&]{
            ContextCompactionSpec spec;spec.id=identity();spec.maintenance_attempt_id=identity();
            spec.snapshot=result.snapshot;spec.selection=*selection;spec.policy=policy.compaction;
            if(manual)spec.manual_request_id=manual->id;
            auto prefix=trusted;prefix.canonical_window=window;
            std::vector<std::int64_t> ordinals;
            for(const auto& group:result.snapshot.groups)if(group.ordinal<=selection->covered_through_ordinal)ordinals.push_back(group.ordinal);
            for(const auto& payload:store.context_group_payloads(result.snapshot,ordinals,bound).get())
                for(const auto& original:payload.originals)prefix.messages.push_back(decode(original));
            spec.input_json=serialize_responses_compaction_request(provider,prefix);spec.input_binding=context_digest(spec.input_json);
            if(idle)spec.admission=IdleContextAdmission{owner.owner_run_id,policy.compaction.max_maintenance_calls,policy.compaction.maintenance_deadline_ms};
            else if(failed)spec.admission=RebuildContextAdmission{owner.root_run_id,owner.owner_run_id,step_id,failed->inference.attempt_id,identity(),owner.role,owner.planning_call_id,budget->snapshot().revision};
            else if(held)spec.admission=PlanningContextAdmission{owner.root_run_id,owner.owner_run_id,step_id,held->attempt_id,*owner.planning_call_id,budget->snapshot().revision};
            else spec.admission=ActiveContextAdmission{owner.root_run_id,owner.owner_run_id,step_id,identity(),owner.role,budget->snapshot().revision};
            funded=admit_with_current_budget([&](std::int64_t revision){
                std::visit([&](auto& admission){
                    using Admission=std::decay_t<decltype(admission)>;
                    if constexpr(!std::is_same_v<Admission,IdleContextAdmission>)admission.expected_budget_revision=revision;
                },spec.admission);
            },[&]{return store.begin_context_compaction(spec).get();});
            compaction_id=funded->compaction_id;
            if(funded->inference)owned_attempt_id=funded->inference->attempt_id;
            return std::pair{std::move(spec),std::move(prefix)};
        };
        std::optional<std::pair<ContextCompactionSpec,ModelRequest>> admission;
        if(idle)admission=admit();
        // Unknown capacities do not cause automatic provider maintenance. A
        // manual request and an explicitly proved overflow are separate paths.
        const bool measure_input=idle||failed||manual||(capacity&&capacity->verified&&policy.compaction.automatic);
        try{
            if(measure_input)result.measure=count(result.snapshot,result.request);
            const auto evaluation=evaluate(result.snapshot,result.request,result.measure);
            const bool compact=idle||failed||manual||evaluation.automatic_compaction;
            if(!evaluation.serialized_bytes_fit)throw ContextCapacityExceeded("Model context exceeds its native wire bound");
            if(compact){
                if(!selection)throw ContextCapacityExceeded("There is no complete reclaimable context prefix");
                if(!admission)admission=admit();
                auto& spec=admission->first;auto& prefix=admission->second;
                check();store.start_context_compaction(spec.id,spec.maintenance_attempt_id).get();
                const auto started=std::chrono::steady_clock::now();maintenance_started=started;
                const auto actual=compact_responses_context(transport_config(),prefix,bearer,cancel);
                ContextCompactionCommit commit;commit.compaction_id=spec.id;commit.maintenance_attempt_id=spec.maintenance_attempt_id;
                commit.binding=owner.binding;commit.expected_head_revision=result.snapshot.head.revision;
                commit.source_watermark=result.snapshot.source_watermark;commit.source_manifest_binding=selection->source_manifest_binding;
                commit.protected_tail_binding=selection->protected_tail_binding;commit.actual_response_json=actual.response_json;
                commit.projection_json=actual.window.items_json();commit.actual_usage_json=actual.usage_json;
                commit.provider_response_id=actual.response_id;commit.provider_created_at=actual.created_at;commit.measured_elapsed_ms=elapsed(started);
                try{store.record_context_compaction_response(commit).get();}
                catch(...){throw ContextOutcomeUnrecorded("Acknowledged compaction result could not be recorded");}
                if(actual.window.items_json().size()>policy.compaction.max_checkpoint_bytes)
                    throw ContextCapacityExceeded("Acknowledged context window exceeds its registered checkpoint bound");
                auto prospective=project(result.snapshot,trusted,selection->covered_through_ordinal,actual.window);
                const auto after=count(result.snapshot,prospective);
                const auto after_evaluation=evaluate(result.snapshot,prospective,after);
                if(!result.measure||after.value>=result.measure->value||!after_evaluation.serialized_bytes_fit||
                   (after_evaluation.input_fit&&!*after_evaluation.input_fit)||
                   (after_evaluation.token_fit&&!*after_evaluation.token_fit))
                    throw ContextCapacityExceeded("Compaction did not produce a smaller compatible context");
                commit.before_measure_id=result.measure->id;commit.after_measure_id=after.id;
                commit.prospective_input_binding=after.payload_binding;
                commit.measured_preparation_elapsed_ms=elapsed(preparation_started);
                try{result.projection=store.commit_context_compaction(commit).get();}
                catch(...){throw ContextOutcomeUnrecorded("Compaction checkpoint publication could not be recorded");}
                result.request=std::move(prospective);result.measure=after;
                result.snapshot=store.context_snapshot(owner.scope,owner.binding,bound).get();
            }
        }catch(const ContextOutcomeUnrecorded&){throw;}
         catch(...){
            const auto failure=std::current_exception();
            if(admission)retire({admission->first.id,admission->first.maintenance_attempt_id,
                failure_code(failure),
                maintenance_started?elapsed(*maintenance_started):0});
            else if(manual&&!manual->owner_id){
                try{store.retire_context_request(manual->id,owner.binding,failure_code(failure)).get();}
                catch(...){throw ContextOutcomeUnrecorded("Manual context request retirement could not be recorded");}
            }
            std::rethrow_exception(failure);
        }
        if(idle)return result;
        check();const auto encoded=serialize_responses_request(provider,result.request);
        const auto input_binding=context_digest(encoded);
        if(failed){
            if(!funded||!funded->inference)throw ContextOutcomeUnrecorded("Rebuild has no funded physical member");
            InferenceRebuildSpec spec{step_id,failed->inference.attempt_id,funded->inference->attempt_id,*compaction_id,
                owner.binding,result.snapshot.head.revision,result.snapshot.source_watermark,budget->snapshot().revision,input_binding};
            const auto rebuilt=admit_with_current_budget([&](std::int64_t revision){spec.expected_budget_revision=revision;},
                [&]{return store.reserve_inference_rebuild(spec).get();});
            const auto member=std::find_if(rebuilt.physical_attempts.begin(),rebuilt.physical_attempts.end(),[&](const auto& value){return value.attempt_id==spec.retry_attempt_id;});
            if(member==rebuilt.physical_attempts.end())throw ContextOutcomeUnrecorded("Rebuild member binding was not recorded");
            result.inference=*member;
        }else{
            InferenceStepSpec spec;spec.id=step_id;spec.root_run_id=owner.root_run_id;spec.owner_run_id=owner.owner_run_id;
            spec.attempt_id=funded&&funded->inference?funded->inference->attempt_id:held?held->attempt_id:identity();
            owned_attempt_id=spec.attempt_id;
            spec.role=owner.role;spec.scope=owner.scope;spec.binding=owner.binding;
            spec.head_revision=result.snapshot.head.revision;spec.source_watermark=result.snapshot.source_watermark;
            spec.expected_budget_revision=budget->snapshot().revision;spec.input_binding=input_binding;spec.planning_call_id=owner.planning_call_id;
            const auto reserved=admit_with_current_budget([&](std::int64_t revision){spec.expected_budget_revision=revision;},
                [&]{return store.reserve_inference_step(spec).get();});
            const auto member=std::find_if(reserved.physical_attempts.begin(),reserved.physical_attempts.end(),[&](const auto& value){return value.attempt_id==spec.attempt_id;});
            if(member==reserved.physical_attempts.end())throw ContextOutcomeUnrecorded("Inference member binding was not recorded");
            result.inference=*member;
        }
        result.step_id=step_id;return result;
    }catch(const ContextOutcomeUnrecorded&){throw;}
     catch(...){const auto failure=std::current_exception();retire_undispatched_inference();std::rethrow_exception(failure);}
};
}
std::string context_route_identity(const ChatProviderConfig& provider,const std::string& identity_json){
    using Json=nlohmann::json;
    if(provider.wire!=ProviderWire::responses||provider.endpoint.empty()||provider.endpoint.size()>16384||identity_json.size()>4096)
        throw ContextBindingChanged("Context route has no bounded Responses destination");
    Json identity;try{identity=Json::parse(identity_json);}catch(const Json::exception&){throw ContextBindingChanged("Context route has no valid provider identity");}
    if(!identity.is_object()||(identity.size()!=2&&identity.size()!=6)||identity.value("wire",Json{})!="responses"||identity.value("model_id",Json{})!=provider.model)
        throw ContextBindingChanged("Context route differs from its selected provider identity");
    const std::string route=identity.contains("route_id")?identity.at("route_id").get<std::string>():std::string{};
    return context_digest("native.context.route.v1:"+std::to_string(provider.endpoint.size())+":"+provider.endpoint+":9:responses:"+std::to_string(route.size())+":"+route);
}
ContextManager::ContextManager(PersistenceService& store,ChatProviderConfig provider,ContextRuntimePolicy policy,MessageDecoder decoder)
    :store_(store),provider_(std::move(provider)),policy_(std::move(policy)),decode_(std::move(decoder)){
    const auto& p=policy_.compaction;
    if(provider_.wire!=ProviderWire::responses||!decode_||p.id!="native.context"||p.revision!=1||
       p.strategy_id!="responses.compact"||p.strategy_revision!=1||p.max_groups<1||p.max_groups>4096||
       p.max_messages<1||p.max_messages>8192||p.max_request_bytes<1||p.max_request_bytes>8*1024*1024||
       p.max_checkpoint_bytes<1||p.max_checkpoint_bytes>responses_context_window_limit||p.max_maintenance_calls<1||p.max_maintenance_calls>4||
       p.max_count_requests<1||p.max_count_requests>32||p.maintenance_deadline_ms<1||p.maintenance_deadline_ms>120000||
       p.retain_recent_groups<1||p.retain_recent_groups>64||policy_.buffer_tokens<0||policy_.model_capacities.size()>256)
        throw std::invalid_argument("Invalid registered native context runtime policy");
}
PreparedModelContext ContextManager::prepare(const ContextOwner& owner,const ModelRequest& trusted,RootExecutionBudget& budget,
    const SecretBytes* bearer,std::stop_token cancel){
    auto actual=Preparation(store_,provider_,policy_,decode_,owner,&budget,bearer,cancel).run(trusted);
    if(!actual.inference)throw ContextOutcomeUnrecorded("Prepared context has no inference reservation");
    return {std::move(actual.request),std::move(*actual.inference),std::move(actual.step_id),std::move(actual.snapshot),std::move(actual.measure)};
}
PreparedModelContext ContextManager::rebuild(const ContextOwner& owner,const ModelRequest& trusted,const PreparedModelContext& failed,
    RootExecutionBudget& budget,const SecretBytes* bearer,std::stop_token cancel){
    auto actual=Preparation(store_,provider_,policy_,decode_,owner,&budget,bearer,cancel).run(trusted,&failed);
    if(!actual.inference)throw ContextOutcomeUnrecorded("Rebuilt context has no physical inference member");
    return {std::move(actual.request),std::move(*actual.inference),std::move(actual.step_id),std::move(actual.snapshot),std::move(actual.measure)};
}
ContextProjection ContextManager::compact_idle(const ContextScope& scope,const ContextBinding& binding,const std::string& id,
    const ModelRequest& trusted,const SecretBytes* bearer,std::stop_token cancel,
    std::optional<std::chrono::steady_clock::time_point> owner_deadline){
    ContextOwner owner;owner.scope=scope;owner.binding=binding;owner.owner_run_id=id;
    auto actual=Preparation(store_,provider_,policy_,decode_,owner,nullptr,bearer,cancel,owner_deadline).run(trusted);
    if(!actual.projection)throw ContextOutcomeUnrecorded("Idle maintenance returned without a checkpoint");
    return std::move(*actual.projection);
}
}
