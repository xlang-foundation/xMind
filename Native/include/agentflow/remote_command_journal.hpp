#pragma once
#include "agentflow/persistence_service.hpp"

namespace agentflow {
// Backend-private intent, never an IPC transport call ID or live authorization.
// Credential revision fences the original local authorization snapshot; the
// remote peer independently derives and authorizes its actor from credentials.
struct RemoteSessionCreateIntent {
    std::string request_id,connection_id,origin,server_id,scope_id,title;
    std::int64_t profile_revision=0,credential_revision=0;
};
enum class RemoteCommandState {prepared,unknown,admitted,rejected};
struct RemoteCommandRecord {
    RemoteSessionCreateIntent intent;
    RemoteCommandState state=RemoteCommandState::prepared;
    std::int64_t revision=1;
    std::string resource_id;
    std::int64_t resource_revision=0;
};
struct RemoteCommandResolution {
    std::string request_id,server_id,scope_id,operation,resource_id;
    RemoteCommandState state=RemoteCommandState::unknown;
    std::int64_t resource_revision=0;
};
// Durable intent/one-send boundary only. No networking, effect execution,
// automatic retries or authorization is provided by this journal.
class RemoteCommandJournal {
public:
    explicit RemoteCommandJournal(PersistenceService& store):store_(store){}
    RemoteCommandRecord prepare(const RemoteSessionCreateIntent&);
    RemoteCommandRecord observe(const std::string& request_id);
    // Persist unknown BEFORE sending. Exactly one caller can claim prepared.
    // Restart in unknown must reconcile; it must not send this intent again.
    bool claim_send(const RemoteSessionCreateIntent&);
    RemoteCommandRecord resolve(const RemoteSessionCreateIntent&,const RemoteCommandResolution&);
private:
    PersistenceService& store_;
};
}
