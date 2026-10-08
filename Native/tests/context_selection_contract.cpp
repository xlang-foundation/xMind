// Pure native policy contracts over explicitly synthetic immutable metadata.
// No payload, provider, database, SDK or execution fixture is used here.
#include "agentflow/context_selection.hpp"
#include <functional>
#include <iostream>
#include <limits>

using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected context policy rejection did not occur");}
ConversationGroup item(const ContextScope& scope,std::int64_t ordinal,ConversationGroupKind kind,std::int64_t first,std::int64_t last){
    ConversationGroup result;result.scope=scope;result.ordinal=ordinal;result.first_message_seq=first;result.last_message_seq=last;
    result.kind=kind;result.message_count=kind==ConversationGroupKind::tool_turn?2:1;result.payload_bytes=100+ordinal;
    result.source_binding="synthetic_source_"+std::to_string(ordinal);
    if(kind!=ConversationGroupKind::user){result.execution_owner="synthetic_root";result.committed_event_seq=ordinal+10;}return result;
}
ContextSnapshot snapshot(){
    ContextSnapshot result;result.head.scope={ContextScopeKind::execution,"synthetic_root"};
    result.head.binding.provider_identity_json=R"({"model_id":"synthetic-model","wire":"responses"})";result.head.binding.authority_identity=std::string(64,'a');
    result.source_watermark=6;result.latest_user_ordinal=1;
    result.groups={item(result.head.scope,1,ConversationGroupKind::user,1,1),item(result.head.scope,2,ConversationGroupKind::tool_turn,2,4),
        item(result.head.scope,3,ConversationGroupKind::assistant,5,5),item(result.head.scope,4,ConversationGroupKind::tool_turn,6,8),
        item(result.head.scope,5,ConversationGroupKind::assistant,9,9),item(result.head.scope,6,ConversationGroupKind::tool_turn,10,11)};
    return result;
}
ContextCapacityRequest request(){
    const auto source=snapshot();ContextCapacityRequest result;result.scope=source.head.scope;result.binding=source.head.binding;
    result.head_revision=2;result.source_watermark=6;result.payload_binding="synthetic_final_wire_binding";result.wire="responses";result.model_id="synthetic-model";
    result.route_identity=context_digest("synthetic actual configured endpoint https://route-a.invalid/v1/responses");
    result.capacity_revision=7;result.capacity_source_binding="synthetic_verified_catalogue_binding";result.buffer_tokens=100;result.serialized_bytes=2048;
    ContextInputMeasure measure;measure.id="synthetic_count";measure.owner_id="synthetic_root";measure.scope=result.scope;measure.binding=result.binding;
    measure.payload_binding=result.payload_binding;measure.model_id=result.model_id;measure.head_revision=result.head_revision;measure.source_watermark=result.source_watermark;
    measure.semantics=ContextMeasureSemantics::provider_exact;measure.value=750;measure.serialized_bytes=result.serialized_bytes;measure.measured_elapsed_ms=3;
    result.measure=measure;result.requested_output_tokens=300;return result;
}
VerifiedContextCapacity capacity(){
    const auto source=request();VerifiedContextCapacity result;result.provider_identity_json=source.binding.provider_identity_json;result.wire=source.wire;result.model_id=source.model_id;
    result.route_identity=source.route_identity;
    result.revision=source.capacity_revision;result.source_binding=source.capacity_source_binding;result.verified=true;
    result.input_tokens=1000;result.context_tokens=1200;result.output_tokens=500;return result;
}
void selection_contract(){
    ContextPolicy policy;const auto source=snapshot();const auto selected=select_context(source,policy);
    require(selected&&selected->covered_through_ordinal==4&&selected->protected_from_ordinal==5,"Settled groups after the current prompt must remain reclaimable");
    require(selected->protected_user_ordinals==std::vector<std::int64_t>{1},"Latest objective must be independently pinned, never a prefix cutoff");
    require(selected->source_manifest_binding==context_source_binding(source,4)&&selected->protected_tail_binding==context_tail_binding(source,5)&&
        selected->protected_user_binding==context_user_binding(source,{1}),"Repository and controller must use the exact same private binding helpers");
    require(selected->source_manifest_binding.size()==64&&selected->source_manifest_binding!=selected->protected_tail_binding&&
        selected->protected_tail_binding!=selected->protected_user_binding,"Different metadata domains must have independent native digests");
    auto small=source;small.source_watermark=2;small.groups.resize(2);require(!select_context(small,policy),"A tail-only snapshot must not dispatch maintenance");
    ContextSnapshot empty;empty.head.scope=source.head.scope;empty.head.binding=source.head.binding;require(!select_context(empty,policy),"An empty fresh scope must not invent context work");
    auto interleaved=source;interleaved.groups[1].last_message_seq=100;interleaved.groups[2].first_message_seq=101;interleaved.groups[2].last_message_seq=101;
    for(std::size_t n=3;n<interleaved.groups.size();++n){interleaved.groups[n].first_message_seq+=100;interleaved.groups[n].last_message_seq+=100;}
    require(select_context(interleaved,policy).has_value(),"Scope-local committed groups may contain global message-sequence gaps");
    auto legacy=source;legacy.groups[1].legacy_unattributed=true;legacy.groups[1].execution_owner.reset();legacy.groups[1].committed_event_seq.reset();
    require(select_context(legacy,policy).has_value(),"Native-validated legacy metadata must not fabricate owner/event attribution");
    auto checkpoint=source;checkpoint.head.revision=2;checkpoint.head.checkpoint_id="synthetic_checkpoint";checkpoint.head.covered_through_ordinal=2;
    checkpoint.groups.erase(checkpoint.groups.begin(),checkpoint.groups.begin()+2);checkpoint.protected_users={source.groups[0]};
    const auto repeated=select_context(checkpoint,policy);require(repeated&&repeated->covered_through_ordinal==4&&repeated->protected_user_ordinals==std::vector<std::int64_t>{1},
        "A new rolling prefix must retain indexed users older than the committed head");
    auto multiple=source;multiple.groups[2]=item(multiple.head.scope,3,ConversationGroupKind::user,5,5);multiple.latest_user_ordinal=3;
    require(select_context(multiple,policy)->protected_user_ordinals==std::vector<std::int64_t>({1,3}),"Canonical source users must remain ordered and independently protected");
    multiple.groups[4]=item(multiple.head.scope,5,ConversationGroupKind::user,9,9);multiple.latest_user_ordinal=5;
    require(select_context(multiple,policy)->protected_user_ordinals==std::vector<std::int64_t>({1,3,5}),"Latest user in the protected tail must still be independently bound");
    const std::vector<std::function<void(ContextSnapshot&)>> invalid_metadata={
        [](auto& v){v.groups.erase(v.groups.begin()+1);},[](auto& v){++v.source_watermark;},[](auto& v){v.groups[1].ordinal=3;},
        [](auto& v){std::swap(v.groups[2],v.groups[3]);},[](auto& v){v.groups[1].message_count=1;},[](auto& v){v.groups[1].message_count=4;},
        [](auto& v){v.groups[1].committed_event_seq.reset();},[](auto& v){v.groups[1].execution_owner.reset();},
        [](auto& v){v.groups[1].scope.id="foreign_scope";},[](auto& v){v.latest_user_ordinal=2;},[](auto& v){v.latest_user_ordinal=0;},
        [](auto& v){v.head.covered_through_ordinal=1;},[](auto& v){v.head.revision=1;},[](auto& v){v.groups[2].source_binding=std::string(1,static_cast<char>(0xff));},
        [](auto& v){v.groups[2].payload_bytes=-1;},[](auto& v){v.groups[2].first_message_seq=4;},
        [](auto& v){v.protected_users={v.groups[0],v.groups[0]};},[](auto& v){auto pinned=v.groups[0];++pinned.payload_bytes;v.protected_users={pinned};},
        [](auto& v){v.protected_users={v.groups[1]};},[](auto& v){v.groups[0].kind=static_cast<ConversationGroupKind>(99);}
    };
    for(const auto& change:invalid_metadata){auto value=source;change(value);rejects<std::invalid_argument>([&]{select_context(value,policy);});rejects<std::invalid_argument>([&]{context_source_binding(value,4);});}
    rejects<std::invalid_argument>([&]{context_source_binding(source,0);});rejects<std::invalid_argument>([&]{context_source_binding(source,7);});
    rejects<std::invalid_argument>([&]{context_tail_binding(source,0);});rejects<std::invalid_argument>([&]{context_tail_binding(source,7);});
    for(const auto& users:{std::vector<std::int64_t>{},std::vector<std::int64_t>{1,1},std::vector<std::int64_t>{2},std::vector<std::int64_t>{3,1}})
        rejects<std::invalid_argument>([&]{context_user_binding(source,users);});
    for(const auto& change:std::vector<std::function<void(ContextPolicy&)>>{
        [](auto& p){p.retain_recent_groups=0;},[](auto& p){p.max_groups=5;},[](auto& p){p.max_messages=8;},[](auto& p){p.revision=2;},
        [](auto& p){p.max_request_bytes=0;},[](auto& p){p.max_maintenance_calls=0;},[](auto& p){p.strategy_id="foreign_strategy";}}){auto value=policy;change(value);rejects<std::invalid_argument>([&]{select_context(source,value);});}
}
void digest_contract(){
    const auto source=snapshot();const auto digest=context_source_binding(source,4);
    const std::vector<std::function<void(ContextSnapshot&)>> changes={
        [](auto& v){v.groups[1].first_message_seq=3;},[](auto& v){v.groups[1].last_message_seq=3;},[](auto& v){v.groups[1].message_count=3;},
        [](auto& v){++v.groups[1].payload_bytes;},[](auto& v){++*v.groups[1].committed_event_seq;},[](auto& v){v.groups[1].execution_owner="another_root";},
        [](auto& v){v.groups[1].legacy_unattributed=true;},[](auto& v){v.groups[1].source_binding="different_exact_binding";},
        [](auto& v){v.groups[1].kind=ConversationGroupKind::assistant;v.groups[1].message_count=1;},
        [](auto& v){v.head.scope.kind=ContextScopeKind::session;for(auto& g:v.groups)g.scope=v.head.scope;},
        [](auto& v){v.head.scope.id="other_scope";for(auto& g:v.groups)g.scope=v.head.scope;},
        [](auto& v){v.head.binding.provider_identity_json+=" ";},[](auto& v){v.head.binding.authority_identity[0]='b';},
        [](auto& v){v.head.binding.policy_id+=".other";},[](auto& v){++v.head.binding.policy_revision;},
        [](auto& v){v.head.binding.strategy_id+=".other";},[](auto& v){++v.head.binding.strategy_revision;},
        [](auto& v){v.head.revision=1;v.head.checkpoint_id="checkpoint";},
        [](auto& v){++v.source_watermark;v.groups.push_back(item(v.head.scope,7,ConversationGroupKind::assistant,12,12));}
    };
    for(const auto& change:changes){auto value=source;change(value);require(context_source_binding(value,4)!=digest,"Every immutable selected group/head/binding field must affect source binding");}
    auto tail=source;tail.groups.back().source_binding="different_tail";require(context_source_binding(tail,4)==digest&&context_tail_binding(tail,5)!=context_tail_binding(source,5),
        "Selected-prefix fields and exact protected-tail fields must have independent coverage");
    auto users=source;users.groups[0].source_binding="different_user";require(context_user_binding(users,{1})!=context_user_binding(source,{1}),"Protected original user source binding must be authoritative");
    auto utf8a=source,utf8b=source;utf8a.groups[1].source_binding="\xc3\xa9";utf8b.groups[1].source_binding="e\xcc\x81";
    require(context_source_binding(utf8a,4)!=context_source_binding(utf8b,4),"UTF-8 text must not be normalized by a digest helper");
    auto fields_a=source,fields_b=source;fields_a.groups[1].source_binding="bc";fields_a.groups[1].execution_owner="a";fields_b.groups[1].source_binding="c";fields_b.groups[1].execution_owner="ab";
    require(context_source_binding(fields_a,4)!=context_source_binding(fields_b,4),"Length prefixes must prevent adjacent field concatenation collisions");
    auto nul_a=source,nul_b=source;nul_a.groups[1].source_binding=std::string("a\0bc",4);nul_b.groups[1].source_binding=std::string("ab\0c",4);
    require(context_source_binding(nul_a,4)!=context_source_binding(nul_b,4),"Exact byte lengths must include embedded UTF-8 NUL fields");
    require(context_digest("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"&&
        context_digest("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Shared portable digest must match SHA-256 known vectors");
}
void capacity_contract(){
    auto input=request();auto limits=capacity();const auto evaluated=evaluate_context_capacity(input,limits);
    require(evaluated.verified_capacity&&evaluated.compatible_measure&&evaluated.serialized_bytes_fit&&evaluated.trigger_input_tokens==900&&
        evaluated.trigger_scope==ContextCapacityTriggerScope::total_context&&evaluated.output_allowance_tokens==300&&evaluated.input_percentage==75.0&&
        evaluated.context_percentage==87.5&&evaluated.token_fit==true&&!evaluated.automatic_compaction,"Matching measurement must bind the lower input/total context trigger and actual output reservation");
    input.measure->value=900;require(evaluate_context_capacity(input,limits).automatic_compaction,"The exact trigger boundary must request configured automatic compaction");
    input.automatic_enabled=false;require(!evaluate_context_capacity(input,limits).automatic_compaction,"Manual-only policy must not become automatic");input.automatic_enabled=true;
    input.buffer_tokens=400;require(evaluate_context_capacity(input,limits).trigger_input_tokens==600,"Buffer larger than output must reserve the buffer, never their sum");
    input=request();input.requested_output_tokens=400;limits.context_tokens=1100;require(evaluate_context_capacity(input,limits).trigger_input_tokens==700,"Total context may supply the lower threshold");
    input.measure->semantics=ContextMeasureSemantics::provider_estimate;require(evaluate_context_capacity(input,limits).measure_semantics==ContextMeasureSemantics::provider_estimate,
        "A compatible provider estimate must remain explicitly distinguished from supplied response usage");
    const auto no_limits=evaluate_context_capacity(input);require(!no_limits.input_percentage&&!no_limits.context_percentage&&!no_limits.token_fit&&!no_limits.automatic_compaction,"Unknown model/window must not invent fit or automatic policy");
    for(const auto& change:std::vector<std::function<void(VerifiedContextCapacity&)>>{
        [](auto& v){v.verified=false;},[](auto& v){v.wire="chat";},[](auto& v){v.model_id="foreign_model";},[](auto& v){++v.revision;},
        [](auto& v){v.provider_identity_json+=" ";},[](auto& v){v.source_binding="foreign_catalogue";},
        [](auto& v){v.route_identity=context_digest("synthetic actual configured endpoint https://route-b.invalid/v1/responses");}}){auto value=limits;change(value);const auto result=evaluate_context_capacity(input,value);
        require(!result.verified_capacity&&!result.input_percentage&&!result.context_percentage&&!result.token_fit&&!result.automatic_compaction,"Capacity must match exact verified route/wire/model/revision/source");}
    for(const auto& change:std::vector<std::function<void(ContextInputMeasure&)>>{
        [](auto& v){v.scope.id="foreign_root";},[](auto& v){++v.head_revision;},[](auto& v){++v.source_watermark;},[](auto& v){v.binding.authority_identity[0]='b';},
        [](auto& v){v.payload_binding="stale_payload";},[](auto& v){v.model_id="other_model";},[](auto& v){++v.serialized_bytes;},
        [](auto& v){v.semantics=ContextMeasureSemantics::serialized_bytes;}}){auto value=input;change(*value.measure);const auto result=evaluate_context_capacity(value,limits);
        require(!result.compatible_measure&&!result.input_percentage&&!result.context_percentage&&!result.token_fit&&!result.automatic_compaction,"Foreign/stale/count-free measurements must not authorize token policy");}
    input=request();input.requested_output_tokens.reset();limits=capacity();const auto unknown_output=evaluate_context_capacity(input,limits);
    require(unknown_output.input_percentage==75.0&&unknown_output.trigger_input_tokens==900&&unknown_output.trigger_scope==ContextCapacityTriggerScope::input_only&&
        !unknown_output.context_percentage&&!unknown_output.token_fit&&!unknown_output.automatic_compaction,"Unknown output with applicable context ceiling cannot authorize a total trigger");
    limits.default_output_tokens=300;require(evaluate_context_capacity(input,limits).output_allowance_tokens==300,"Only an explicitly verified provider/model default may reserve omitted output");
    input=request();input.requested_output_tokens.reset();limits=capacity();limits.context_tokens.reset();limits.output_tokens.reset();input.measure->value=900;
    const auto input_only=evaluate_context_capacity(input,limits);require(input_only.input_percentage==90.0&&input_only.input_fit==true&&!input_only.token_fit&&!input_only.context_percentage&&
        input_only.trigger_scope==ContextCapacityTriggerScope::input_only&&input_only.automatic_compaction,"Verified input-only evidence may trigger input-only work but cannot claim a full window fit");
    input=request();limits=capacity();limits.input_tokens.reset();require(evaluate_context_capacity(input,limits).trigger_input_tokens==900&&
        !evaluate_context_capacity(input,limits).input_percentage&&evaluate_context_capacity(input,limits).token_fit==true,"Verified total-only ceiling can evaluate output plus input without inventing an input ceiling");
    input=request();input.serialized_bytes=input.max_serialized_bytes+1;input.measure->serialized_bytes=input.serialized_bytes;input.measure->value=900;
    const auto bytes=evaluate_context_capacity(input,capacity());require(!bytes.serialized_bytes_fit&&!bytes.automatic_compaction,"Independent final serialized byte limit must not be bypassed by token fit");
    input=request();input.measure.reset();input.serialized_bytes=input.max_serialized_bytes;require(evaluate_context_capacity(input,capacity()).serialized_bytes_fit&&
        !evaluate_context_capacity(input,capacity()).automatic_compaction,"A byte count alone must not initiate token-based compaction");
    input=request();input.route_identity.clear();const auto missing_route=evaluate_context_capacity(input,capacity());
    require(!missing_route.verified_capacity&&!missing_route.input_percentage&&!missing_route.context_percentage&&!missing_route.token_fit&&!missing_route.automatic_compaction,
        "An empty actual route binding must not inherit a capacity from a generic wire/model identity");
    for(const auto& value:{std::string{},std::string(63,'a'),std::string(64,'A'),std::string(64,'g')}){auto bad=capacity();bad.route_identity=value;
        rejects<std::invalid_argument>([&]{evaluate_context_capacity(request(),bad);});}
    input=request();input.route_identity=context_digest("synthetic actual configured endpoint https://route-b.invalid/v1/responses");
    require(input.binding.provider_identity_json==capacity().provider_identity_json&&!evaluate_context_capacity(input,capacity()).verified_capacity,
        "Same wire/model metadata across distinct actual endpoints must not authorize the other route's capacity");
    for(const auto value:{0LL,-1LL}){input=request();input.requested_output_tokens=value;rejects<std::invalid_argument>([&]{evaluate_context_capacity(input,capacity());});
        auto bad=capacity();bad.default_output_tokens=value;rejects<std::invalid_argument>([&]{evaluate_context_capacity(request(),bad);});}
    input=request();input.requested_output_tokens=501;rejects<ContextCapacityExceeded>([&]{evaluate_context_capacity(input,capacity());});
    input.measure.reset();rejects<ContextCapacityExceeded>([&]{evaluate_context_capacity(input,capacity());});
    input=request();input.buffer_tokens=1000;rejects<ContextCapacityExceeded>([&]{evaluate_context_capacity(input,capacity());});
    input=request();limits=capacity();limits.input_tokens.reset();input.buffer_tokens=1200;rejects<ContextCapacityExceeded>([&]{evaluate_context_capacity(input,limits);});
    input=request();limits=capacity();limits.context_tokens=300;rejects<ContextCapacityExceeded>([&]{evaluate_context_capacity(input,limits);});
    input=request();input.measure->value=std::numeric_limits<std::int64_t>::max();limits=capacity();limits.input_tokens=std::numeric_limits<std::int64_t>::max();
    limits.context_tokens=std::numeric_limits<std::int64_t>::max();limits.output_tokens=std::numeric_limits<std::int64_t>::max();input.requested_output_tokens=std::numeric_limits<std::int64_t>::max()-1;input.buffer_tokens=0;
    const auto huge=evaluate_context_capacity(input,limits);require(huge.trigger_input_tokens==1&&huge.token_fit==false&&huge.context_percentage&&*huge.context_percentage>100,
        "Window sums and threshold subtraction must remain safe at int64 capacity boundaries");
    for(const auto& change:std::vector<std::function<void(ContextCapacityRequest&)>>{
        [](auto& v){v.buffer_tokens=-1;},[](auto& v){v.serialized_bytes=-1;},[](auto& v){v.max_serialized_bytes=0;},
        [](auto& v){v.measure->value=-1;},[](auto& v){v.measure->measured_elapsed_ms=-1;},[](auto& v){v.measure->semantics=static_cast<ContextMeasureSemantics>(99);}}){auto value=request();change(value);rejects<std::invalid_argument>([&]{evaluate_context_capacity(value,capacity());});}
}
}
int main(){try{selection_contract();digest_contract();capacity_contract();std::cout<<"Pure native context selection/capacity contracts passed over synthetic metadata; no payload, SQL or provider execution\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
