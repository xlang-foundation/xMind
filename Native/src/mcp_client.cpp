#include "agentflow/mcp_client.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <deque>
#include <set>

namespace agentflow {
namespace {
using Json=nlohmann::json;
std::string bounded_text(const Json& value,const char* key,std::size_t limit,bool required=true) {
    if(!value.contains(key)) {if(!required)return {};throw McpProtocolError("Missing MCP tool description field");}
    if(!value[key].is_string())throw McpProtocolError("Invalid MCP tool description field");
    auto text=value[key].get<std::string>();
    if(text.size()>limit || text.find('\0')!=std::string::npos || (required && text.empty()))throw McpProtocolError("MCP tool description exceeds limits");
    return text;
}
McpToolPage tool_page(const McpWireMessage& message) {
    if(message.kind==McpMessageKind::error)throw McpProtocolError("MCP peer rejected tool discovery");
    const auto value=Json::parse(message.payload_json);
    if(value.contains("resultType") && value["resultType"]!="complete")throw McpProtocolError("MCP tool discovery requires a complete result");
    if(!value.contains("tools") || !value["tools"].is_array() || value["tools"].size()>128)throw McpProtocolError("Invalid or excessive MCP tool page");
    McpToolPage page;std::set<std::string> names;
    for(const auto& item:value["tools"]) {
        if(!item.is_object())throw McpProtocolError("Invalid MCP tool description");
        auto name=bounded_text(item,"name",128);
        if(!names.insert(name).second)throw McpProtocolError("Duplicate MCP tool name in page");
        if(!item.contains("inputSchema") || !item["inputSchema"].is_object())throw McpProtocolError("Missing MCP input schema");
        if(item.contains("annotations") && !item["annotations"].is_object())throw McpProtocolError("Invalid MCP tool annotations");
        page.tools.push_back({std::move(name),bounded_text(item,"description",65536,false),item["inputSchema"].dump(),item.contains("annotations")?item["annotations"].dump():"{}"});
    }
    if(value.contains("nextCursor"))page.next_cursor=bounded_text(value,"nextCursor",4096);
    return page;
}
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
                page=tool_page(reply->response);
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
const McpServerDescription& McpStdioClient::server() const {if(!ready())throw McpProtocolError("MCP client is not ready");return impl_->handshake.server();}
McpStdioStatus McpStdioClient::status() const {return impl_->process.status();}
void McpStdioClient::shutdown() noexcept {if(impl_ && !impl_->closed)impl_->retire();}
}
