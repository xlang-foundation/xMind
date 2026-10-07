#include "agentflow/permission_waiter.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace agentflow {
Operation PermissionWaiter::acquire(const std::string& id,const OperationSpec& spec,std::int64_t expiry,std::stop_token cancel) {
    if(cancel.stop_requested()) throw PermissionCancelled("Operation cancelled before proposal");
    persistence_.request_operation(id,spec,expiry).get();
    std::mutex mutex;std::condition_variable_any changed;std::unique_lock lock(mutex);
    for(;;) {
        if(cancel.stop_requested()) {
            // A decision may race cancellation; neither state authorizes an
            // effect here. Never change an already claimed operation's outcome.
            try {persistence_.cancel_operation(id,spec).get();} catch(const Conflict&) {}
            throw PermissionCancelled("Operation permission wait cancelled");
        }
        const auto current=persistence_.operation(id).get();
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if((current.state==OperationState::awaiting_approval || current.state==OperationState::ready) && current.expires_unix_ms<=now) {
            try {persistence_.expire_operation(id).get();} catch(const Conflict&) {}
            continue;
        }
        switch(current.state) {
        case OperationState::ready:
            try {
                auto claimed=persistence_.claim_operation(id,spec).get();
                // Ownership has transferred. The effect adapter performs its
                // pre-dispatch cancellation check and journals the outcome;
                // this permission layer must not hide a claimed operation if
                // outcome storage fails during a cancellation race.
                return claimed;
            } catch(const WorkspaceEffectBusy&) {
                // Retry only the durable permission claim, never a tool effect.
            } catch(const WorkspaceEffectUncertain&) {
                try {persistence_.cancel_operation(id,spec).get();} catch(const Conflict&) {}
                throw;
            } catch(const Conflict&) {
                // Expiry or owner cancellation can commit between the state
                // read and claim. Observe that persisted outcome on the next
                // pass; a still-ready identity/owner conflict is not retryable.
                if(persistence_.operation(id).get().state==OperationState::ready) throw;
                continue;
            }
            break;
        case OperationState::awaiting_approval:break;
        case OperationState::denied:throw PermissionDenied("Operation permission denied");
        case OperationState::expired:throw PermissionExpired("Operation permission expired");
        case OperationState::cancelled:throw PermissionCancelled("Operation permission cancelled");
        default:throw Conflict("Operation no longer belongs to this permission wait");
        }
        // Durable decisions are the authority. Polling also observes repository
        // changes from maintenance or future database adapters. Cancellation
        // wakes immediately; notification/subscription optimization is separate.
        changed.wait_for(lock,cancel,std::chrono::milliseconds(100),[]{return false;});
    }
}
}
