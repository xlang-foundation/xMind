#include "agentflow/context_selection.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>

namespace agentflow { namespace {
constexpr std::size_t metadata_group_limit=4096,metadata_byte_limit=1024*1024;
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid context selection metadata");}
bool utf8(std::string_view text){
    for(std::size_t i=0;i<text.size();){
        const auto a=static_cast<unsigned char>(text[i++]);
        if(a<0x80)continue;
        unsigned count=0;std::uint32_t value=0,minimum=0;
        if(a>=0xc2&&a<=0xdf){count=1;value=a&31;minimum=0x80;}
        else if(a>=0xe0&&a<=0xef){count=2;value=a&15;minimum=0x800;}
        else if(a>=0xf0&&a<=0xf4){count=3;value=a&7;minimum=0x10000;}
        else return false;
        if(text.size()-i<count)return false;
        for(unsigned n=0;n<count;++n){const auto b=static_cast<unsigned char>(text[i++]);if((b&0xc0)!=0x80)return false;value=(value<<6)|(b&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
void text_field(std::string_view value,bool nonempty=true){if((nonempty&&value.empty())||value.size()>metadata_byte_limit||!utf8(value))invalid();}
void scope(const ContextScope& value){if(value.kind!=ContextScopeKind::session&&value.kind!=ContextScopeKind::execution)invalid();text_field(value.id);}
void binding(const ContextBinding& value){
    text_field(value.provider_identity_json);text_field(value.authority_identity);text_field(value.policy_id);text_field(value.strategy_id);
    if(value.policy_revision<=0||value.strategy_revision<=0)invalid();
}
bool same_group(const ConversationGroup& a,const ConversationGroup& b){
    return a.scope==b.scope&&a.ordinal==b.ordinal&&a.first_message_seq==b.first_message_seq&&a.last_message_seq==b.last_message_seq&&
        a.execution_owner==b.execution_owner&&a.committed_event_seq==b.committed_event_seq&&a.kind==b.kind&&
        a.message_count==b.message_count&&a.payload_bytes==b.payload_bytes&&a.source_binding==b.source_binding&&a.legacy_unattributed==b.legacy_unattributed;
}
void group(const ConversationGroup& value,const ContextScope& expected){
    if(value.scope!=expected||value.ordinal<=0||value.first_message_seq<=0||value.last_message_seq<value.first_message_seq||
        value.message_count<=0||value.message_count>value.last_message_seq-value.first_message_seq+1||value.payload_bytes<=0)invalid();
    if(value.kind!=ConversationGroupKind::user&&value.kind!=ConversationGroupKind::assistant&&value.kind!=ConversationGroupKind::tool_turn)invalid();
    if((value.kind==ConversationGroupKind::tool_turn&&value.message_count<2)||(value.kind!=ConversationGroupKind::tool_turn&&value.message_count!=1))invalid();
    if(value.execution_owner)text_field(*value.execution_owner);
    if(value.committed_event_seq&&*value.committed_event_seq<=0)invalid();
    if(value.kind!=ConversationGroupKind::user&&!value.legacy_unattributed&&(!value.execution_owner||!value.committed_event_seq))invalid();
    text_field(value.source_binding);
}
struct ValidSnapshot {std::map<std::int64_t,const ConversationGroup*> users;std::uint64_t message_count=0;};
ValidSnapshot validate(const ContextSnapshot& value){
    scope(value.head.scope);binding(value.head.binding);
    if(value.head.revision<0||value.head.covered_through_ordinal<0||value.source_watermark<value.head.covered_through_ordinal||
        value.latest_user_ordinal<0||value.latest_user_ordinal>value.source_watermark)invalid();
    if((value.head.revision==0&&(value.head.covered_through_ordinal!=0||value.head.checkpoint_id))||
        (value.head.revision>0&&!value.head.checkpoint_id))invalid();
    if(value.head.checkpoint_id)text_field(*value.head.checkpoint_id);
    const auto count=static_cast<std::uint64_t>(value.source_watermark-value.head.covered_through_ordinal);
    if(count!=value.groups.size()||value.groups.size()>metadata_group_limit||value.protected_users.size()>metadata_group_limit)invalid();
    ValidSnapshot result;std::int64_t ordinal=value.head.covered_through_ordinal,last_seq=0;
    std::size_t bytes=value.head.scope.id.size()+value.head.binding.provider_identity_json.size()+value.head.binding.authority_identity.size()+
        value.head.binding.policy_id.size()+value.head.binding.strategy_id.size()+(value.head.checkpoint_id?value.head.checkpoint_id->size():0);
    if(bytes>metadata_byte_limit)invalid();
    const auto account=[&](const ConversationGroup& item){
        const auto size=item.source_binding.size()+item.scope.id.size()+(item.execution_owner?item.execution_owner->size():0)+128;
        if(size>metadata_byte_limit||bytes>metadata_byte_limit-size)invalid();bytes+=size;
        if(static_cast<std::uint64_t>(item.message_count)>std::numeric_limits<std::uint64_t>::max()-result.message_count)invalid();
        result.message_count+=static_cast<std::uint64_t>(item.message_count);
    };
    for(const auto& item:value.groups){
        if(ordinal==std::numeric_limits<std::int64_t>::max())invalid();++ordinal;
        group(item,value.head.scope);if(item.ordinal!=ordinal||item.first_message_seq<=last_seq)invalid();last_seq=item.last_message_seq;
        account(item);if(item.kind==ConversationGroupKind::user)result.users.emplace(item.ordinal,&item);
    }
    std::int64_t previous=0;
    for(const auto& item:value.protected_users){
        group(item,value.head.scope);if(item.kind!=ConversationGroupKind::user||item.ordinal<=previous||item.ordinal>value.source_watermark)invalid();previous=item.ordinal;
        const auto current=result.users.find(item.ordinal);
        if(item.ordinal>value.head.covered_through_ordinal){if(current==result.users.end()||!same_group(item,*current->second))invalid();}
        else account(item);
        if(current==result.users.end())result.users.emplace(item.ordinal,&item);
    }
    if(value.source_watermark==0){if(value.latest_user_ordinal!=0||!result.users.empty())invalid();}
    else if(value.latest_user_ordinal==0||result.users.empty()||result.users.rbegin()->first!=value.latest_user_ordinal)invalid();
    return result;
}
// A fixed big-endian uint64 byte count precedes EVERY field (including numbers
// and optional presence markers). Field boundaries cannot collide with text.
class Frame {
public:
    void field(std::string_view value){const auto size=static_cast<std::uint64_t>(value.size());for(int n=56;n>=0;n-=8)bytes_.push_back(static_cast<char>((size>>n)&255));bytes_.append(value);}
    void number(std::int64_t value){field(std::to_string(value));}
    void optional(const std::optional<std::string>& value){field(value?"present":"absent");if(value)field(*value);}
    void optional(const std::optional<std::int64_t>& value){field(value?"present":"absent");if(value)number(*value);}
    std::string digest()const{return context_digest(bytes_);}
private:std::string bytes_;
};
void frame_scope(Frame& out,const ContextScope& value){out.field(value.kind==ContextScopeKind::session?"session":"execution");out.field(value.id);}
void frame_group(Frame& out,const ConversationGroup& value){
    frame_scope(out,value.scope);out.number(value.ordinal);out.number(value.first_message_seq);out.number(value.last_message_seq);
    out.optional(value.execution_owner);out.optional(value.committed_event_seq);
    out.field(value.kind==ConversationGroupKind::user?"user":value.kind==ConversationGroupKind::assistant?"assistant":"tool_turn");
    out.number(value.message_count);out.number(value.payload_bytes);out.field(value.source_binding);out.field(value.legacy_unattributed?"true":"false");
}
Frame frame_head(const ContextSnapshot& value,std::string_view domain){
    Frame out;out.field("native.context.metadata.v1");out.field(domain);frame_scope(out,value.head.scope);
    const auto& b=value.head.binding;out.field(b.provider_identity_json);out.field(b.authority_identity);out.field(b.policy_id);out.number(b.policy_revision);
    out.field(b.strategy_id);out.number(b.strategy_revision);out.number(value.head.revision);out.optional(value.head.checkpoint_id);
    out.number(value.head.covered_through_ordinal);out.number(value.source_watermark);out.number(value.latest_user_ordinal);return out;
}
void positive(std::optional<std::int64_t> value){if(value&&*value<=0)throw std::invalid_argument("Invalid context capacity metadata");}
bool route_identity(std::string_view value){return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
bool compatible(const ContextInputMeasure& measure,const ContextCapacityRequest& request){
    if(measure.semantics!=ContextMeasureSemantics::provider_exact&&measure.semantics!=ContextMeasureSemantics::provider_estimate&&measure.semantics!=ContextMeasureSemantics::serialized_bytes)
        throw std::invalid_argument("Invalid context measure semantics");
    if(measure.value<0||measure.serialized_bytes<0||measure.measured_elapsed_ms<0||measure.head_revision<0||measure.source_watermark<0)
        throw std::invalid_argument("Invalid context measure metadata");
    return measure.scope==request.scope&&measure.binding==request.binding&&measure.head_revision==request.head_revision&&
        measure.source_watermark==request.source_watermark&&measure.payload_binding==request.payload_binding&&measure.model_id==request.model_id&&measure.serialized_bytes==request.serialized_bytes;
}
} // namespace

std::string context_source_binding(const ContextSnapshot& snapshot,std::int64_t covered){
    validate(snapshot);if(covered<=snapshot.head.covered_through_ordinal||covered>snapshot.source_watermark)invalid();
    auto out=frame_head(snapshot,"source");out.number(covered);out.number(covered-snapshot.head.covered_through_ordinal);
    for(const auto& item:snapshot.groups)if(item.ordinal<=covered)frame_group(out,item);return out.digest();
}
std::string context_tail_binding(const ContextSnapshot& snapshot,std::int64_t from){
    validate(snapshot);if(from<=snapshot.head.covered_through_ordinal||from>snapshot.source_watermark)invalid();
    auto out=frame_head(snapshot,"tail");out.number(from);out.number(snapshot.source_watermark-from+1);
    for(const auto& item:snapshot.groups)if(item.ordinal>=from)frame_group(out,item);return out.digest();
}
std::string context_user_binding(const ContextSnapshot& snapshot,const std::vector<std::int64_t>& ordinals){
    const auto validated=validate(snapshot);if(ordinals.size()>metadata_group_limit)invalid();
    auto out=frame_head(snapshot,"users");out.number(static_cast<std::int64_t>(ordinals.size()));std::int64_t previous=0;
    for(const auto ordinal:ordinals){if(ordinal<=previous)invalid();previous=ordinal;const auto at=validated.users.find(ordinal);if(at==validated.users.end())invalid();frame_group(out,*at->second);}
    if(snapshot.latest_user_ordinal>0&&!std::binary_search(ordinals.begin(),ordinals.end(),snapshot.latest_user_ordinal))invalid();
    for(const auto& item:snapshot.protected_users)if(!std::binary_search(ordinals.begin(),ordinals.end(),item.ordinal))invalid();
    return out.digest();
}
std::optional<ContextSelection> select_context(const ContextSnapshot& snapshot,const ContextPolicy& policy){
    const auto validated=validate(snapshot);
    if(policy.id!=snapshot.head.binding.policy_id||policy.revision!=snapshot.head.binding.policy_revision||policy.strategy_id!=snapshot.head.binding.strategy_id||
        policy.strategy_revision!=snapshot.head.binding.strategy_revision||policy.max_groups==0||policy.max_groups>metadata_group_limit||policy.max_messages==0||
        policy.retain_recent_groups==0||policy.retain_recent_groups>policy.max_groups||policy.max_request_bytes==0||policy.max_checkpoint_bytes==0||
        policy.max_maintenance_calls<=0||policy.max_count_requests<=0||policy.maintenance_deadline_ms<=0||snapshot.groups.size()>policy.max_groups||validated.message_count>policy.max_messages)invalid();
    if(snapshot.groups.size()<=policy.retain_recent_groups)return std::nullopt;
    const auto& tail=snapshot.groups[snapshot.groups.size()-policy.retain_recent_groups];
    ContextSelection result;result.protected_from_ordinal=tail.ordinal;result.covered_through_ordinal=tail.ordinal-1;
    for(const auto& item:snapshot.protected_users)result.protected_user_ordinals.push_back(item.ordinal);
    for(const auto& item:snapshot.groups)if(item.kind==ConversationGroupKind::user&&(item.ordinal<=result.covered_through_ordinal||item.ordinal==snapshot.latest_user_ordinal))result.protected_user_ordinals.push_back(item.ordinal);
    std::sort(result.protected_user_ordinals.begin(),result.protected_user_ordinals.end());result.protected_user_ordinals.erase(std::unique(result.protected_user_ordinals.begin(),result.protected_user_ordinals.end()),result.protected_user_ordinals.end());
    result.source_manifest_binding=context_source_binding(snapshot,result.covered_through_ordinal);
    result.protected_tail_binding=context_tail_binding(snapshot,result.protected_from_ordinal);
    result.protected_user_binding=context_user_binding(snapshot,result.protected_user_ordinals);return result;
}
ContextCapacityEvaluation evaluate_context_capacity(const ContextCapacityRequest& request,const std::optional<VerifiedContextCapacity>& capacity){
    scope(request.scope);binding(request.binding);text_field(request.payload_binding);text_field(request.wire);text_field(request.model_id);
    if(request.head_revision<0||request.source_watermark<0||request.capacity_revision<0||request.buffer_tokens<0||request.serialized_bytes<0||request.max_serialized_bytes<=0)
        throw std::invalid_argument("Invalid context capacity request");
    if(!request.route_identity.empty()&&!route_identity(request.route_identity))throw std::invalid_argument("Invalid context capacity route");
    positive(request.requested_output_tokens);
    ContextCapacityEvaluation result;result.serialized_bytes_fit=request.serialized_bytes<=request.max_serialized_bytes;
    if(request.measure&&compatible(*request.measure,request)){
        result.measure_semantics=request.measure->semantics;result.compatible_measure=request.measure->semantics!=ContextMeasureSemantics::serialized_bytes;
    }
    if(!capacity)return result;
    positive(capacity->input_tokens);positive(capacity->output_tokens);positive(capacity->context_tokens);positive(capacity->default_output_tokens);
    if(capacity->revision<0||(capacity->default_output_tokens&&capacity->output_tokens&&*capacity->default_output_tokens>*capacity->output_tokens))
        throw std::invalid_argument("Invalid context capacity metadata");
    if(!capacity->verified)return result;
    text_field(capacity->provider_identity_json);text_field(capacity->wire);text_field(capacity->model_id);text_field(capacity->source_binding);
    if(capacity->revision<=0||!route_identity(capacity->route_identity))throw std::invalid_argument("Invalid context capacity metadata");
    result.verified_capacity=capacity->provider_identity_json==request.binding.provider_identity_json&&capacity->wire==request.wire&&capacity->model_id==request.model_id&&
        capacity->revision==request.capacity_revision&&capacity->source_binding==request.capacity_source_binding&&
        route_identity(request.route_identity)&&capacity->route_identity==request.route_identity;
    if(!result.verified_capacity)return result;
    if(capacity->input_tokens&&request.buffer_tokens>=*capacity->input_tokens)throw ContextCapacityExceeded("Context input buffer leaves no usable capacity");
    if(capacity->context_tokens&&request.buffer_tokens>=*capacity->context_tokens)throw ContextCapacityExceeded("Context buffer leaves no usable capacity");
    const auto output=request.requested_output_tokens?request.requested_output_tokens:capacity->default_output_tokens;
    if(output&&capacity->output_tokens){
        if(*output>*capacity->output_tokens)throw ContextCapacityExceeded("Requested output exceeds verified capacity");
        if(capacity->context_tokens&&std::max(*output,request.buffer_tokens)>=*capacity->context_tokens)
            throw ContextCapacityExceeded("Output reservation leaves no usable context capacity");
        result.output_allowance_tokens=output;
    }
    if(!result.compatible_measure)return result;
    const auto input=request.measure->value;
    if(capacity->input_tokens){
        result.trigger_input_tokens=*capacity->input_tokens-request.buffer_tokens;result.trigger_scope=ContextCapacityTriggerScope::input_only;
        result.input_percentage=static_cast<double>(input)/static_cast<double>(*capacity->input_tokens)*100.0;result.input_fit=input<=*capacity->input_tokens;
    }
    if(capacity->context_tokens){
        if(!result.output_allowance_tokens)return result;
        const auto reserve=std::max(*result.output_allowance_tokens,request.buffer_tokens);
        const auto threshold=*capacity->context_tokens-reserve;
        result.trigger_input_tokens=result.trigger_input_tokens?std::min(*result.trigger_input_tokens,threshold):threshold;
        result.trigger_scope=ContextCapacityTriggerScope::total_context;
        // Division separately avoids signed overflow when input+output exceeds
        // int64; fit uses subtraction after verified output/context bounds.
        result.context_percentage=(static_cast<double>(input)/static_cast<double>(*capacity->context_tokens)+
            static_cast<double>(*result.output_allowance_tokens)/static_cast<double>(*capacity->context_tokens))*100.0;
        result.token_fit=input<=*capacity->context_tokens-*result.output_allowance_tokens&&(!result.input_fit||*result.input_fit);
    }
    if(result.trigger_input_tokens)result.automatic_compaction=request.automatic_enabled&&result.serialized_bytes_fit&&input>=*result.trigger_input_tokens;
    return result;
}
} // namespace agentflow
