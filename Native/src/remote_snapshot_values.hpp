#pragma once
#include "agentflow/remote_snapshot_store.hpp"
#include "remote_event_values.hpp"

namespace agentflow::remote_snapshot_values {
// Public observation fields. xlang3 SDK owns IPC value/reference transfer.
inline nlohmann::json assembly(const RemoteSnapshotAssembly& value,const std::string& generation){
    auto records=nlohmann::json::array();for(const auto& record:value.records)records.push_back({{"kind",record.kind},{"id",record.id},{"revision",record.revision},{"payload",nlohmann::json::parse(record.payload_json)}});
    return {{"consumer_id",value.base.consumer.id},{"checkpoint_revision",value.base.checkpoint_revision},{"accepted",remote_event_values::cursor(value.base.accepted,generation)},{"snapshot_id",value.snapshot_id},{"revision",value.revision},{"cursor",remote_event_values::cursor(value.cursor,generation)},{"total_records",value.total_records},{"records",std::move(records)},{"complete",value.complete},{"digest",remote_snapshot_digest(value)}};
}
inline nlohmann::json reset(const RemoteSnapshotResetReceipt& value){return {{"snapshot_id",value.snapshot_id},{"digest",value.digest},{"snapshot_revision",value.snapshot_revision},{"checkpoint_revision",value.checkpoint_revision}};}
}
