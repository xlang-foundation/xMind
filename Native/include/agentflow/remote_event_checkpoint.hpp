#pragma once
#include "agentflow/remote_connection_client.hpp"

namespace agentflow {
struct RemoteEventConsumer {
    std::string id,origin;
    RemoteRunEventCursor initial;
};
struct RemoteEventCheckpoint {
    RemoteEventConsumer consumer;
    std::int64_t revision=1;
    RemoteRunEventCursor accepted;
    std::optional<RemoteObservedRunEvents> pending;
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
private:
    PersistenceService& store_;
};
}
