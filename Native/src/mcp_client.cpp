#include "agentflow/mcp_client.hpp"
#include "agentflow/mcp_tool_codec.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <deque>

namespace agentflow {
namespace {
using Json=nlohmann::json;
}
struct McpStdioClient::Impl {
    McpStdioProcess process;
    McpRequestTracker requests;
    McpHandshake handshake;
    std::deque<McpWireMessage> incoming;
    std::size_t queued_bytes=0,unsolicited=0;
    McpLineStream wire;
    bool started=false,live=false,closed=false;
    explicit Impl(const McpStdioConfiguration& configuration):process(configuration),handshake(requests),wire([this](const auto& message){
        const auto size=message.raw_json.size()+message.payload_json.size();
        if(incoming.size()>=128 || size>4*1024*1024-queued_bytes)throw McpProtocolError("MCP incoming messages exceed owner limits");
        incoming.push_back(message);queued_bytes+=size;
    }) {}
    void read(Deadline deadline,std::stop_token cancel) {
        const auto bytes=process.read(deadline,cancel);
        if(!bytes){wire.finish();throw McpTransportError("MCP peer closed its output");}
        wire.feed(*bytes);
    }
    McpWireMessage pop() {
        auto message=std::move(incoming.front());incoming.pop_front();queued_bytes-=message.raw_json.size()+message.payload_json.size();return message;
    }
    bool peer_message(const McpWireMessage& message,Deadline deadline,std::stop_token cancel) {
        if(message.kind==McpMessageKind::notification || message.kind==McpMessageKind::request) {
            if(++unsolicited>256)throw McpProtocolError("MCP unsolicited message limit exceeded");
            if(message.kind==McpMessageKind::request) {
                if(!handshake.ready())throw McpProtocolError("MCP client request arrived before negotiation");
                process.write(mcp_peer_reply(message,handshake.server().era),deadline,cancel);
            }
            // Notifications are not persisted, exposed to a model or treated as
            // effects. Subscription/progress routing requires separate owners.
            return true;
        }
        return false;
    }
    void retire() noexcept {
        live=false;closed=true;
        try {
            // Best effort cancellation does not establish an effect outcome.
            // Never replay. Initialization is deliberately not wire-cancelled.
            const auto pending=requests.abandon_all();
            for(const auto& request:pending)if(request.method!="initialize") {
                try {process.write(mcp_notification("notifications/cancelled",Json{{"requestId",request.id}}.dump()),std::chrono::steady_clock::now()+std::chrono::milliseconds(100));}catch(...) {break;}
            }
        }catch(...) {}
        incoming.clear();queued_bytes=0;process.shutdown();
    }
};
McpStdioClient::McpStdioClient(const McpStdioConfiguration& configuration):impl_(std::make_unique<Impl>(configuration)) {}
McpStdioClient::~McpStdioClient(){shutdown();}
void McpStdioClient::connect(Deadline deadline,std::stop_token cancel,std::chrono::milliseconds probe_window) {
    auto& state=*impl_;
    if(state.started || state.closed)throw McpProtocolError("MCP owner cannot reconnect or replay negotiation");
    if(probe_window<=std::chrono::milliseconds::zero() || probe_window>std::chrono::seconds(30))throw std::invalid_argument("Invalid MCP probe window");
    state.started=true;state.unsolicited=0;bool probing=true;
    const auto probe_deadline=std::min(deadline,std::chrono::steady_clock::now()+probe_window);
    try {
        state.process.write(state.handshake.begin().frame,deadline,cancel);
        while(!state.handshake.ready()) {
            if(state.incoming.empty()) {
                try {state.read(probing?probe_deadline:deadline,cancel);}
                catch(const McpTransportTimeout&) {
                    if(!probing || std::chrono::steady_clock::now()>=deadline)throw;
                    probing=false;state.process.write(state.handshake.probe_timeout().frame,deadline,cancel);continue;
                }
            }
            while(!state.incoming.empty()) {
                const auto message=state.pop();if(state.peer_message(message,deadline,cancel))continue;
                const auto reply=state.requests.receive(message);if(!reply)continue;
                const auto action=state.handshake.accept(*reply);
                if(action.request){probing=false;state.process.write(action.request->frame,deadline,cancel);}
                if(!action.notification.empty()){state.process.write(action.notification,deadline,cancel);state.handshake.initialized_sent();}
            }
        }
        state.live=true;
    }catch(...) {state.retire();throw;}
}
McpToolPage McpStdioClient::list_tools(std::optional<std::string> cursor,Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;if(!ready()){if(state.live)state.retire();throw McpProtocolError("MCP client is not ready");}
    if(cursor && (cursor->empty() || cursor->size()>4096 || cursor->find('\0')!=std::string::npos))throw std::invalid_argument("Invalid MCP tool cursor");
    if(!Json::parse(state.handshake.server().capabilities_json).contains("tools"))throw McpProtocolError("MCP server did not advertise tools");
    state.unsolicited=0;Json params=Json::object();if(cursor)params["cursor"]=*cursor;
    try {
        const auto request=state.requests.prepare("tools/list",params.dump(),state.handshake.server().era);
        state.process.write(request.frame,deadline,cancel);std::optional<McpToolPage> page;
        while(!page) {
            if(state.incoming.empty())state.read(deadline,cancel);
            while(!state.incoming.empty()) {
                const auto message=state.pop();if(state.peer_message(message,deadline,cancel))continue;
                const auto reply=state.requests.receive(message);if(!reply)continue;
                if(reply->request.id!=request.request.id)throw McpProtocolError("MCP discovery response identity changed");
                page=mcp_decode_tool_page(reply->response);
            }
        }
        return std::move(*page);
    }catch(...) {state.retire();throw;}
}
bool McpStdioClient::ready() const {
    if(!impl_->live || impl_->closed)return false;
    const auto observed=impl_->process.status();
    return !observed.closed && !observed.faulted && !observed.exit_code;
}
McpToolReply McpStdioClient::call_tool(const std::string& name,std::string_view arguments,Deadline deadline,std::stop_token cancel) {
    auto& state=*impl_;bool attempted=false;std::string request_id,response_json;
    try {
        if(!ready() || cancel.stop_requested() || std::chrono::steady_clock::now()>=deadline)throw McpTransportError("MCP tool stopped before dispatch");
        if(name.empty() || name.size()>128 || name.find('\0')!=std::string::npos || arguments.size()>65536)throw McpProtocolError("Invalid MCP tool call");
        const auto params="{\"name\":"+Json(name).dump()+",\"arguments\":"+mcp_compact_object(arguments)+"}";
        const auto request=state.requests.prepare("tools/call",params,state.handshake.server().era);request_id=request.request.id;
        if(cancel.stop_requested() || std::chrono::steady_clock::now()>=deadline)throw McpTransportError("MCP tool stopped before dispatch");
        state.unsolicited=0;attempted=true;state.process.write(request.frame,deadline,cancel);
        std::optional<McpToolReply> result;
        while(!result) {
            if(state.incoming.empty())state.read(deadline,cancel);
            while(!state.incoming.empty()) {
                const auto message=state.pop();if(state.peer_message(message,deadline,cancel))continue;
                const auto reply=state.requests.receive(message);if(!reply)continue;
                if(reply->request.id!=request_id)throw McpProtocolError("MCP tool response identity changed");
                response_json=reply->response.raw_json;
                result=mcp_decode_tool_reply(request_id,reply->response);
            }
        }
        return std::move(*result);
    }catch(...) {state.retire();throw McpDispatchFailure(attempted,std::move(request_id),std::move(response_json));}
}
const McpServerDescription& McpStdioClient::server() const {if(!ready())throw McpProtocolError("MCP client is not ready");return impl_->handshake.server();}
McpStdioStatus McpStdioClient::status() const {return impl_->process.status();}
void McpStdioClient::shutdown() noexcept {if(impl_ && !impl_->closed)impl_->retire();}
}
