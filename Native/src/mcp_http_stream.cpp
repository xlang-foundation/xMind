#include "agentflow/mcp_http_stream.hpp"
#include "nlohmann/json.hpp"
#include <utility>

namespace agentflow {
namespace {
constexpr std::size_t message_limit=1024*1024,stream_limit=8*1024*1024;
void append(std::string& target,std::string_view bytes) {
    if(bytes.size()>message_limit-target.size())throw McpProtocolError("MCP HTTP message exceeds limits");
    target.append(bytes);
}
void utf8(const std::string& line) {
    try {(void)nlohmann::json(line).dump();}catch(const nlohmann::json::exception&) {throw McpProtocolError("Invalid MCP HTTP UTF-8");}
}
}
struct McpHttpMessageStream::Impl {
    McpLineStream::Sink sink;
    bool sse=false,first_line=true,skip_lf=false,failed=false,closed=false,has_data=false;
    std::size_t received=0,messages=0;
    std::string line,data,event;
    Impl(std::string_view media,McpLineStream::Sink consumer):sink(std::move(consumer)) {
        if(!sink)throw std::invalid_argument("MCP HTTP requires a message sink");
        if(media=="text/event-stream")sse=true;
        else if(media!="application/json")throw McpProtocolError("Unsupported MCP HTTP message media");
    }
    void deliver(const std::string& json) {
        if(++messages>256)throw McpProtocolError("MCP HTTP response message count exceeds limits");
        sink(mcp_decode_message(json));
    }
    void end_line() {
        utf8(line);
        if(first_line){if(line.starts_with("\xef\xbb\xbf"))line.erase(0,3);first_line=false;}
        if(line.empty()) {
            if(has_data) {
                if(!event.empty() && event!="message")throw McpProtocolError("Unsupported MCP HTTP SSE event");
                // SSE adds one line terminator to each data field. Remove only
                // that final terminator; retain JSON whitespace and raw tokens.
                data.pop_back();deliver(data);
            }
            data.clear();event.clear();has_data=false;
        } else if(line.front()!=':') {
            const auto separator=line.find(':');const auto field=line.substr(0,separator);
            auto value=separator==std::string::npos?std::string_view{}:std::string_view(line).substr(separator+1);
            if(value.starts_with(' '))value.remove_prefix(1);
            if(field=="data") {append(data,value);append(data,"\n");has_data=true;}
            else if(field=="event"){if(value.size()>128)throw McpProtocolError("MCP HTTP SSE event name exceeds limits");event=value;}
            // id/retry/unknown fields confer no reconnect or scheduling authority.
        }
        line.clear();
    }
};
McpHttpMessageStream::McpHttpMessageStream(std::string_view media_type,McpLineStream::Sink sink):impl_(std::make_unique<Impl>(media_type,std::move(sink))) {}
McpHttpMessageStream::~McpHttpMessageStream()=default;
void McpHttpMessageStream::feed(std::string_view bytes) {
    auto& state=*impl_;if(state.failed || state.closed)throw McpProtocolError("MCP HTTP stream is retired");
    try {
        if(bytes.size()>stream_limit-state.received)throw McpProtocolError("MCP HTTP stream exceeds limits");state.received+=bytes.size();
        if(!state.sse){append(state.data,bytes);return;}
        for(char c:bytes) {
            if(state.skip_lf){state.skip_lf=false;if(c=='\n')continue;}
            if(c=='\r'){state.end_line();state.skip_lf=true;}
            else if(c=='\n')state.end_line();
            else append(state.line,std::string_view(&c,1));
        }
    }catch(...){state.failed=true;throw;}
}
void McpHttpMessageStream::finish() {
    auto& state=*impl_;if(state.failed || state.closed)throw McpProtocolError("MCP HTTP stream is retired");
    try {
        if(!state.sse)state.deliver(state.data);
        else if(!state.line.empty() || state.has_data || !state.event.empty())throw McpProtocolError("Incomplete MCP HTTP SSE event at EOF");
        state.closed=true;
    }catch(...){state.failed=true;throw;}
}
}
