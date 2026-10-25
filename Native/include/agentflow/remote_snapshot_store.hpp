#pragma once
#include "agentflow/remote_event_checkpoint.hpp"
#include "agentflow/remote_run_snapshot.hpp"

namespace agentflow {
struct RemoteSnapshotBase {
    RemoteEventConsumer consumer;
    std::int64_t checkpoint_revision=0;
    RemoteRunEventCursor accepted;
};
struct RemoteSnapshotAssembly {
    RemoteSnapshotBase base;
    std::string snapshot_id;
    std::int64_t revision=0,total_records=0;
    RemoteRunEventCursor cursor;
    std::vector<RemoteSnapshotRecord> records;
    bool complete=false;
    std::string last_page_digest;
};
// Backend-private observation storage. Authorization belongs to the captured
// service. A completed assembly does not acknowledge/reset an event checkpoint.
class RemoteRunSnapshots {
public:
    explicit RemoteRunSnapshots(PersistenceService& store):store_(store){}
    RemoteSnapshotAssembly observe(const RemoteSnapshotBase&,const std::string& snapshot_id);
    RemoteSnapshotAssembly stage(const RemoteSnapshotBase&,std::int64_t expected_revision,const RemoteRunSnapshotPage&);
private:PersistenceService& store_;
};
}
