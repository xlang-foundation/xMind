#include "agentflow/remote_event_checkpoint.hpp"
#include "agentflow/remote_snapshot_store.hpp"
#include "agentflow/connection_profiles.hpp"
#include "nlohmann/json.hpp"

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr auto category="remote-event-checkpoint-v1";
constexpr std::int64_t maximum=9007199254740991LL;
bool identifier(const std::string& value){return !value.empty()&&value.size()<=256&&value!="."&&value!=".."&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-:")==std::string::npos;}
bool same_scope(const RemoteRunEventCursor& a,const RemoteRunEventCursor& b){return a.connection_id==b.connection_id&&a.server_id==b.server_id&&a.scope_id==b.scope_id&&a.session_id==b.session_id&&a.run_id==b.run_id;}
void valid(const RemoteRunEventCursor& value){
    if(!identifier(value.connection_id)||!identifier(value.server_id)||!identifier(value.scope_id)||!identifier(value.session_id)||!identifier(value.run_id)||value.sequence<0||value.sequence>maximum||(!value.epoch.empty()&&!identifier(value.epoch))||(value.epoch.empty()&&value.sequence!=0))throw std::invalid_argument("Invalid remote event checkpoint cursor");
}
void valid(const RemoteEventConsumer& value){valid(value.initial);validate_remote_connection_origin(value.origin);if(!identifier(value.id)||!value.initial.epoch.empty()||value.initial.sequence!=0)throw std::invalid_argument("Invalid remote event consumer");}
void revision(std::int64_t value){if(value<1||value>=maximum)throw std::invalid_argument("Invalid remote event checkpoint revision");}
Json cursor(const RemoteRunEventCursor& v){return {{"connection_id",v.connection_id},{"server_id",v.server_id},{"scope_id",v.scope_id},{"session_id",v.session_id},{"run_id",v.run_id},{"epoch",v.epoch},{"sequence",v.sequence}};}
RemoteRunEventCursor cursor(const Json& v){
    if(!v.is_object()||v.size()!=7||!v.at("sequence").is_number_integer()||v.at("sequence")<0||v.at("sequence")>maximum)throw DatabaseError("Invalid remote event checkpoint");
    RemoteRunEventCursor result{v.at("connection_id"),v.at("server_id"),v.at("scope_id"),v.at("session_id"),v.at("run_id"),v.at("epoch"),v.at("sequence")};valid(result);return result;
}
void page(const RemoteRunEventCursor& after,const RemoteObservedRunEvents& value){
    valid(value.cursor);if(!same_scope(after,value.cursor)||value.cursor.epoch.empty()||value.events.size()>256)throw std::invalid_argument("Invalid remote event checkpoint page");
    if(!after.epoch.empty()&&after.epoch!=value.cursor.epoch)throw RemoteEventCursorGap(RemoteEventGapReason::epoch_expired);
    std::int64_t sequence=after.sequence;std::size_t bytes=0;
    try{for(const auto& event:value.events){bytes+=event.json.size()+event.run_id.size()+event.kind.size()+sizeof(event.sequence);if(bytes>65536||event.sequence<=sequence||event.sequence>maximum||event.run_id!=after.run_id||!identifier(event.kind)||event.kind.size()>128||!Json::parse(event.json).is_object())throw std::invalid_argument("Invalid remote event checkpoint event");sequence=event.sequence;}}
    catch(const Json::exception&){throw std::invalid_argument("Invalid remote event checkpoint payload");}
    if(value.cursor.sequence!=sequence)throw std::invalid_argument("Remote checkpoint skips unobserved events");
}
Json page(const RemoteObservedRunEvents& value){auto events=Json::array();for(const auto& event:value.events)events.push_back({{"sequence",event.sequence},{"run_id",event.run_id},{"kind",event.kind},{"json",event.json}});return {{"cursor",cursor(value.cursor)},{"events",std::move(events)}};}
std::string encode(const RemoteEventCheckpoint& value){Json encoded={{"version",1},{"id",value.consumer.id},{"origin",value.consumer.origin},{"initial",cursor(value.consumer.initial)},{"revision",value.revision},{"accepted",cursor(value.accepted)},{"pending",value.pending?page(*value.pending):Json(nullptr)}};if(value.snapshot_reset){const auto& r=*value.snapshot_reset;encoded["snapshot_reset"]={{"snapshot_id",r.snapshot_id},{"digest",r.digest},{"snapshot_revision",r.snapshot_revision},{"checkpoint_revision",r.checkpoint_revision}};}return encoded.dump();}
RemoteEventCheckpoint decode(const std::string& raw){
    try{
        const auto value=Json::parse(raw);if(!value.is_object()||(value.size()!=7&&value.size()!=8)||!value.at("version").is_number_integer()||value.at("version")!=1||!value.at("revision").is_number_integer()||value.at("revision")<1||value.at("revision")>maximum)throw DatabaseError("Invalid remote event checkpoint");
        RemoteEventCheckpoint result;result.consumer={value.at("id"),value.at("origin"),cursor(value.at("initial"))};valid(result.consumer);result.revision=value.at("revision");result.accepted=cursor(value.at("accepted"));if(!same_scope(result.consumer.initial,result.accepted))throw DatabaseError("Invalid remote event checkpoint");
        if(!value.at("pending").is_null()){
            const auto& pending=value.at("pending");if(!pending.is_object()||pending.size()!=2||!pending.at("events").is_array()||pending.at("events").size()>256)throw DatabaseError("Invalid remote event checkpoint");
            RemoteObservedRunEvents observed;observed.cursor=cursor(pending.at("cursor"));for(const auto& event:pending.at("events")){if(!event.is_object()||event.size()!=4||!event.at("sequence").is_number_integer()||event.at("sequence")<0||event.at("sequence")>maximum)throw DatabaseError("Invalid remote event checkpoint");observed.events.push_back({event.at("sequence"),event.at("run_id"),event.at("kind"),event.at("json")});}page(result.accepted,observed);result.pending=std::move(observed);
        }
        if(value.contains("snapshot_reset")){const auto& r=value.at("snapshot_reset");if(!r.is_object()||r.size()!=4||!r.at("snapshot_revision").is_number_integer()||!r.at("checkpoint_revision").is_number_integer())throw DatabaseError("Invalid remote snapshot reset receipt");RemoteSnapshotResetReceipt receipt{r.at("snapshot_id"),r.at("digest"),r.at("snapshot_revision"),r.at("checkpoint_revision")};if(!identifier(receipt.snapshot_id)||receipt.digest.size()!=64||receipt.digest.find_first_not_of("0123456789abcdef")!=std::string::npos||receipt.snapshot_revision<1||receipt.snapshot_revision>maximum||receipt.checkpoint_revision<1||receipt.checkpoint_revision>=result.revision)throw DatabaseError("Invalid remote snapshot reset receipt");result.snapshot_reset=std::move(receipt);}
        if(encode(result)!=raw)throw DatabaseError("Invalid remote event checkpoint");return result;
    }catch(const Json::exception&){throw DatabaseError("Invalid remote event checkpoint");}catch(const std::invalid_argument&){throw DatabaseError("Invalid remote event checkpoint");}catch(const RemoteEventCursorGap&){throw DatabaseError("Invalid remote event checkpoint");}
}
void matching(const RemoteEventCheckpoint& value,const RemoteEventConsumer& consumer){if(value.consumer.id!=consumer.id||value.consumer.origin!=consumer.origin||value.consumer.initial!=consumer.initial)throw Conflict("Remote event consumer binding changed");}
void advance(RemoteEventCheckpoint& value){if(value.revision>=maximum)throw Conflict("Remote event checkpoint revision exhausted");++value.revision;}
}
RemoteEventCheckpoint RemoteEventCheckpoints::observe(const RemoteEventConsumer& consumer){valid(consumer);auto result=decode(store_.information(category,consumer.id).get());matching(result,consumer);return result;}
RemoteEventCheckpoint RemoteEventCheckpoints::open(const RemoteEventConsumer& consumer){valid(consumer);RemoteEventCheckpoint result;result.consumer=consumer;result.accepted=consumer.initial;try{store_.compare_information(category,consumer.id,encode(result),std::nullopt).get();return result;}catch(const Conflict&){return observe(consumer);}}
RemoteEventCheckpoint RemoteEventCheckpoints::stage(const RemoteEventConsumer& consumer,std::int64_t expected,const RemoteObservedRunEvents& observed){
    revision(expected);auto result=observe(consumer);page(result.accepted,observed);
    if(result.pending){if(result.revision==expected+1&&page(*result.pending)==page(observed))return result;throw Conflict("Remote event page already staged");}
    if(result.revision!=expected)throw Conflict("Remote event checkpoint revision changed");if(observed.cursor==result.accepted&&observed.events.empty())return result;
    const auto before=encode(result);result.pending=observed;advance(result);store_.compare_information(category,consumer.id,encode(result),before).get();return result;
}
RemoteEventCheckpoint RemoteEventCheckpoints::accept(const RemoteEventConsumer& consumer,std::int64_t expected,const RemoteRunEventCursor& accepted){
    revision(expected);valid(accepted);auto result=observe(consumer);
    if(!result.pending){if(result.revision==expected+1&&result.accepted==accepted)return result;throw Conflict("No staged remote event page");}
    if(result.revision!=expected||result.pending->cursor!=accepted)throw Conflict("Remote event acknowledgement differs from staged page");
    const auto before=encode(result);result.accepted=accepted;result.pending.reset();advance(result);store_.compare_information(category,consumer.id,encode(result),before).get();return result;
}
RemoteEventCheckpoint RemoteEventCheckpoints::reset(const RemoteSnapshotBase& base,const std::string& snapshot,std::int64_t expected_snapshot,const std::string& digest){
    revision(base.checkpoint_revision);revision(expected_snapshot);
    if(digest.size()!=64||digest.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("Invalid snapshot acknowledgement digest");
    const auto assembly=RemoteRunSnapshots(store_).observe(base,snapshot);
    if(!assembly.complete||assembly.revision!=expected_snapshot||remote_snapshot_digest(assembly)!=digest)throw Conflict("Snapshot acknowledgement does not name a complete stored assembly");
    auto result=observe(base.consumer);const RemoteSnapshotResetReceipt receipt{snapshot,digest,expected_snapshot,base.checkpoint_revision};
    if(result.revision==base.checkpoint_revision+1&&!result.pending&&result.accepted==assembly.cursor&&result.snapshot_reset==receipt)return result;
    if(result.revision!=base.checkpoint_revision||result.accepted!=base.accepted||result.pending)throw Conflict("Snapshot reset baseline changed or an event page is pending");
    const auto before=encode(result);result.accepted=assembly.cursor;result.snapshot_reset=receipt;advance(result);
    store_.compare_information(category,base.consumer.id,encode(result),before).get();return result;
}
}
