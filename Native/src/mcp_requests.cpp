#include "agentflow/mcp_requests.hpp"
#include "nlohmann/json.hpp"
#include <deque>
#include <map>
#include <random>
#include <sstream>
#include <iomanip>
#include <limits>
#include <utility>

namespace agentflow {
struct McpRequestTracker::Impl {
    enum class Retired {completed,cancelled,abandoned};
    std::map<std::string,McpPendingRequest> active;
    std::map<std::string,Retired> retired;
    std::deque<std::string> retired_order;
    std::size_t capacity;
    std::uint64_t sequence=0;
    std::string prefix;
    explicit Impl(std::size_t limit):capacity(limit) {
        if(limit==0 || limit>128) throw std::invalid_argument("MCP pending capacity must be 1-128");
        // Correlation salt only, not an authentication credential or trust grant.
        std::random_device random;std::ostringstream salt;salt<<"xmind-"<<std::hex<<std::setfill('0');
        for(int i=0;i<4;++i) salt<<std::setw(8)<<random();prefix=salt.str()+"-";
    }
    void retire(const std::string& key,Retired reason) {
        retired.emplace(key,reason);retired_order.push_back(key);
        while(retired_order.size()>256) {retired.erase(retired_order.front());retired_order.pop_front();}
    }
};
McpRequestTracker::McpRequestTracker(std::size_t capacity):impl_(std::make_unique<Impl>(capacity)) {}
McpRequestTracker::~McpRequestTracker()=default;
McpPreparedRequest McpRequestTracker::prepare(std::string method,std::string_view params,McpWireEra era,const McpClientIdentity& identity) {
    auto& state=*impl_;
    if(state.active.size()>=state.capacity) throw McpProtocolError("MCP pending request capacity exhausted");
    if(state.sequence==std::numeric_limits<std::uint64_t>::max()) throw McpProtocolError("MCP request identity sequence exhausted");
    const auto id=state.prefix+std::to_string(state.sequence+1);
    const auto frame=mcp_request(id,method,params,era,identity);
    McpPendingRequest request{id,std::move(method)};
    state.active.emplace(nlohmann::json(id).dump(),request);++state.sequence;
    return {std::move(request),frame};
}
std::optional<McpCorrelatedReply> McpRequestTracker::receive(const McpWireMessage& message) {
    if(message.kind!=McpMessageKind::result && message.kind!=McpMessageKind::error) throw McpProtocolError("MCP request tracker accepts responses only");
    auto& state=*impl_;const auto found=state.active.find(message.id_json);
    if(found==state.active.end()) {
        const auto retired=state.retired.find(message.id_json);
        if(retired!=state.retired.end() && retired->second!=Impl::Retired::completed) return std::nullopt;
        throw McpProtocolError(retired==state.retired.end()?"MCP response has an unknown request ID":"Duplicate MCP response");
    }
    McpCorrelatedReply result{found->second,message};
    state.retire(found->first,Impl::Retired::completed);state.active.erase(found);return result;
}
std::string McpRequestTracker::cancel(const std::string& id) {
    auto& state=*impl_;const auto key=nlohmann::json(id).dump();const auto found=state.active.find(key);
    if(found==state.active.end()) throw McpProtocolError("MCP cancellation has no pending request");
    if(found->second.method=="initialize") throw McpProtocolError("MCP initialize cannot be cancelled on the wire");
    const auto notification=mcp_notification("notifications/cancelled",nlohmann::json{{"requestId",id}}.dump());
    state.retire(key,Impl::Retired::cancelled);state.active.erase(found);return notification;
}
std::vector<McpPendingRequest> McpRequestTracker::abandon_all() {
    auto& state=*impl_;std::vector<McpPendingRequest> lost;lost.reserve(state.active.size());
    for(const auto& [key,request]:state.active) {lost.push_back(request);state.retire(key,Impl::Retired::abandoned);}
    state.active.clear();return lost;
}
std::size_t McpRequestTracker::pending() const {return impl_->active.size();}
}
