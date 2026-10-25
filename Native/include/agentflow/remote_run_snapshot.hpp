#pragma once
#include "agentflow/remote_connection_client.hpp"

namespace agentflow {
struct RemoteSnapshotPosition {
    std::string snapshot_id,epoch;
    std::int64_t sequence=0,total_records=0,offset=0;
};
struct RemoteSnapshotRecord {
    std::string kind,id,payload_json;
    std::int64_t revision=0;
};
struct RemoteRunSnapshotPage {
    RemoteRunEventCursor cursor;
    RemoteSnapshotPosition position;
    std::vector<RemoteSnapshotRecord> records;
    bool complete=false;
};
void validate_remote_run_snapshot_request(const RemoteNegotiatedGrant&,
    const RemoteRunEventCursor&,std::int64_t now_unix_seconds,
    const std::optional<RemoteSnapshotPosition>& continuation=std::nullopt);
RemoteRunSnapshotPage read_remote_run_snapshot_page(const std::string& origin,
    const SecretBytes&,const RemoteNegotiatedGrant&,const RemoteRunEventCursor&,
    const std::optional<RemoteSnapshotPosition>&,std::chrono::milliseconds,
    RemoteTransportPolicy=RemoteTransportPolicy::verified_https,std::stop_token={});
// Native network-domain decoder only. The captured service must authorize before
// and after transport. A decoded page neither resets a cursor nor proves that
// all pages have been durably assembled or applied by a consumer.
RemoteRunSnapshotPage validate_remote_run_snapshot_page(std::string_view response,
    const RemoteNegotiatedGrant&,const RemoteRunEventCursor& resource,
    std::int64_t now_unix_seconds,
    const std::optional<RemoteSnapshotPosition>& continuation=std::nullopt);
}
