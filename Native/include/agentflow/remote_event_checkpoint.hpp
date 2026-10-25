#pragma once
#include "agentflow/remote_connection_client.hpp"

namespace agentflow {
struct RemoteSnapshotBase;
struct RemoteSnapshotResetReceipt {
    std::string snapshot_id,digest;
    std::int64_t snapshot_revision=0,checkpoint_revision=0;
    bool operator==(const RemoteSnapshotResetReceipt&)const=default;
};
struct RemoteEventConsumer {
    std::string id,origin;
    RemoteRunEventCursor initial;
};
struct RemoteEventCheckpoint {
    RemoteEventConsumer consumer;
    std::int64_t revision=1;
    RemoteRunEventCursor accepted;
    std::optional<RemoteObservedRunEvents> pending;
    std::optional<RemoteSnapshotResetReceipt> snapshot_reset;
};
// Backend-private observation checkpoint, not authorization or an IPC dispatcher.
// The caller must freshly authorize reads before publishing retained pages.
// stage accepts only a page validated by the native remote observation service.
class RemoteEventCheckpoints {
public:
    explicit RemoteEventCheckpoints(PersistenceService& store):store_(store){}
    RemoteEventCheckpoint open(const RemoteEventConsumer&);
    RemoteEventCheckpoint observe(const RemoteEventConsumer&);
    RemoteEventCheckpoint stage(const RemoteEventConsumer&,std::int64_t expected_revision,const RemoteObservedRunEvents&);
    RemoteEventCheckpoint accept(const RemoteEventConsumer&,std::int64_t expected_revision,const RemoteRunEventCursor&);
    RemoteEventCheckpoint reset(const RemoteSnapshotBase&,const std::string& snapshot_id,
        std::int64_t expected_snapshot_revision,const std::string& snapshot_digest);
private:
    PersistenceService& store_;
};
}
