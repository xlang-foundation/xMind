#include "agentflow/http_stream_transport.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "nlohmann/json.hpp"
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cwctype>
#include <initializer_list>
#include <mutex>

namespace agentflow {
namespace {
using Clock=std::chrono::steady_clock;
struct Handle {
    HINTERNET value;
    explicit Handle(HINTERNET input):value(input) {if(!value) throw TransportError("Cannot create native HTTP handle");}
    ~Handle() {if(value) WinHttpCloseHandle(value);}
    Handle(const Handle&)=delete;
    HINTERNET release() {auto result=value;value=nullptr;return result;}
};
struct WipedHeaders {
    std::wstring value;
    ~WipedHeaders() {if(!value.empty()) SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));}
};
struct State {
    std::mutex mutex;
    std::condition_variable_any changed;
    DWORD status=0,error=0,size=0;
    bool closed=false;
    void prepare() {std::lock_guard lock(mutex);status=0;size=0;}
    DWORD wait(DWORD expected,Clock::time_point deadline,std::stop_token cancel) {
        std::unique_lock lock(mutex);
        const auto ready=changed.wait_until(lock,cancel,deadline,[&]{return status==expected || error!=0;});
        if(cancel.stop_requested()) throw TransportCancelled("Provider request cancelled");
        if(!ready) throw TransportTimeout("Provider request deadline exceeded");
        if(error==ERROR_WINHTTP_TIMEOUT) throw TransportTimeout("Provider request timed out");
        if(error) throw TransportError("Native provider request failed (code "+std::to_string(error)+")");
        return size;
    }
};
void CALLBACK callback(HINTERNET,DWORD_PTR context,DWORD status,LPVOID info,DWORD length) noexcept {
    if(!context) return;
    auto& state=*reinterpret_cast<State*>(context);
    {
        std::lock_guard lock(state.mutex);
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) state.closed=true;
        else if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && length>=sizeof(WINHTTP_ASYNC_RESULT))
            state.error=static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
            status==WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE || status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            state.status=status;
            if(status==WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE && length>=sizeof(DWORD)) state.size=*static_cast<DWORD*>(info);
            if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) state.size=length;
        }
        // Notify while holding the mutex so teardown cannot destroy State while
        // this final callback still uses its condition variable.
        state.changed.notify_all();
    }
}
struct RequestHandle {
    HINTERNET value;
    State& state;
    ~RequestHandle() {
        // The caller owns all API calls. At this point an async operation may be
        // pending, but no thread is inside another WinHTTP call on this handle.
        WinHttpCloseHandle(value);
        std::unique_lock lock(state.mutex);
        state.changed.wait(lock,[&]{return state.closed;});
        // HANDLE_CLOSING is the final callback; state/buffers can now be released.
    }
};
void checked(BOOL ok) {
    if(!ok) throw TransportError("Native HTTP operation failed (code "+std::to_string(GetLastError())+")");
}
std::wstring wide(const std::string& text) {
    if(text.empty()) return {};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!count) throw std::invalid_argument("Invalid UTF-8 endpoint");
    std::wstring result(count,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count)) throw std::invalid_argument("Invalid UTF-8 endpoint");
    return result;
}
std::wstring lower(std::wstring value) {for(auto& c:value) c=static_cast<wchar_t>(std::towlower(c));return value;}
enum class TransferKind {event_post,json_get,json_post,mcp_post};
struct Endpoint {std::wstring host,path;INTERNET_PORT port;bool secure;};
Endpoint endpoint(const std::string& input) {
    if(input.empty() || input.size()>8192)throw std::invalid_argument("Invalid endpoint URL");
    for(unsigned char c:input)if(c<=32 || c==127 || c=='#')throw std::invalid_argument("Invalid endpoint URL");
    const auto url=wide(input);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts))throw std::invalid_argument("Invalid endpoint URL");
    if(parts.dwUserNameLength || parts.dwPasswordLength || input.find('@')!=std::string::npos)throw std::invalid_argument("Endpoint cannot contain credentials");
    Endpoint result;result.host.assign(parts.lpszHostName,parts.dwHostNameLength);result.port=parts.nPort;result.secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
    const auto normalized=lower(result.host);
    if(result.host.empty() || (!result.secure && (parts.nScheme!=INTERNET_SCHEME_HTTP || (normalized!=L"localhost" && normalized!=L"127.0.0.1" && normalized!=L"::1"))))throw std::invalid_argument("Remote endpoints require HTTPS");
    result.path.assign(parts.lpszUrlPath,parts.dwUrlPathLength);if(result.path.empty())result.path=L"/";
    if(parts.dwExtraInfoLength)result.path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);return result;
}
std::optional<std::string> response_field(HINTERNET request,const wchar_t* name,bool combine=false) {
    std::array<wchar_t,8192> buffer{};DWORD index=0;std::optional<std::string> result;
    for(std::size_t count=0;;++count) {
        DWORD size=static_cast<DWORD>(sizeof(buffer));
        if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_CUSTOM,name,buffer.data(),&size,&index)) {
            if(GetLastError()==ERROR_WINHTTP_HEADER_NOT_FOUND)return result;
            throw TransportError("Missing or oversized MCP HTTP response metadata");
        }
        if(count>=16 || size%sizeof(wchar_t) || size>=sizeof(buffer) || (result && !combine))throw TransportError("Ambiguous or oversized MCP HTTP response metadata");
        if(!result)result.emplace();else *result+=", ";
        for(const auto c:std::wstring_view(buffer.data(),size/sizeof(wchar_t))) {
            if(c!=L'\t' && (c<32 || c>126))throw TransportError("Invalid MCP HTTP response metadata");
            result->push_back(static_cast<char>(c));
        }
        if(result->size()>8192)throw TransportError("Oversized MCP HTTP response metadata");
    }
}
}
static void transfer(const HttpStreamRequest& input,const SecretBytes* bearer,
    const std::function<void(std::string_view)>& consume,std::stop_token cancel,
    TransferKind kind,std::size_t response_limit,const McpHttpPost* mcp=nullptr,
    const std::function<void(const McpHttpResponseHead&)>& on_head={},
    const std::function<void()>& on_sending={}) {
    if(!consume || input.url.empty() || input.url.size()>8192 || input.body.size()>8*1024*1024 ||
        input.deadline.count()<=0 || input.deadline.count()>600000 || input.idle_timeout.count()<=0 || input.idle_timeout.count()>600000)
        throw std::invalid_argument("Invalid provider transport configuration");
    std::wstring_view credentialPrefix;
    switch(input.credential_header){
        case CredentialHeader::bearer:credentialPrefix=L"Authorization: Bearer ";break;
        case CredentialHeader::x_api_key:credentialPrefix=L"x-api-key: ";break;
        case CredentialHeader::x_goog_api_key:credentialPrefix=L"x-goog-api-key: ";break;
        default:throw std::invalid_argument("Unsupported provider credential header");
    }
    if(!bearer&&input.credential_header!=CredentialHeader::bearer)throw std::invalid_argument("Provider API-key header requires a credential");
    if(input.protocol!=ProviderHttpProtocol::generic&&input.protocol!=ProviderHttpProtocol::anthropic)
        throw std::invalid_argument("Unsupported provider HTTP protocol");
    if(cancel.stop_requested()) throw TransportCancelled("Provider request cancelled");
    const auto deadline=Clock::now()+input.deadline;
    const auto parsed=endpoint(input.url);
    Handle session(WinHttpOpen(L"xMind/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
    checked(WinHttpSetTimeouts(session.value,10000,10000,10000,static_cast<int>(input.idle_timeout.count())));
    Handle connection(WinHttpConnect(session.value,parsed.host.c_str(),parsed.port,0));
    WipedHeaders headers;
    const auto json=kind!=TransferKind::event_post;
    headers.value=kind==TransferKind::json_get?L"":L"Content-Type: application/json\r\n";
    headers.value+=mcp?L"Accept: application/json, text/event-stream\r\n":json?L"Accept: application/json\r\n":L"Accept: text/event-stream\r\n";
    if(input.protocol==ProviderHttpProtocol::anthropic)headers.value+=L"anthropic-version: 2023-06-01\r\n";
    if(mcp) {
        auto projected=mcp_http_request_headers(input.body,mcp->era,mcp->legacy_protocol);
        if(mcp->input_schema) {
            const auto body=nlohmann::json::parse(mcp_compact_object(input.body));
            if(body["method"]!="tools/call")throw McpProtocolError("MCP header schema requires a tool call");
            const auto params=mcp_object_member(input.body,"params");
            if(!params)throw McpProtocolError("MCP tool call has no parameters");
            const auto arguments=mcp_object_member(*params,"arguments").value_or("{}");
            const auto values=McpHttpToolHeaders(*mcp->input_schema).project(arguments);
            projected.insert(projected.end(),values.begin(),values.end());
        }
        for(const auto& header:projected)headers.value+=wide(header.name)+L": "+wide(header.value)+L"\r\n";
        if(mcp->legacy_session) {
            if(mcp->era!=McpWireEra::legacy || mcp->legacy_session->empty() || mcp->legacy_session->size()>4096)throw McpProtocolError("Invalid MCP HTTP session binding");
            for(unsigned char c:*mcp->legacy_session)if(c<33 || c>126)throw McpProtocolError("Invalid MCP HTTP session binding");
            headers.value+=L"Mcp-Session-Id: "+wide(*mcp->legacy_session)+L"\r\n";
        }
        if(headers.value.size()>64*1024)throw McpProtocolError("MCP HTTP headers exceed native limits");
    }
    if(bearer) {
        const auto bytes=bearer->view();
        if(bytes.empty() || bytes.size()>32768) throw std::invalid_argument("Invalid provider credential");
        // Reserve before copying secret bytes so growth cannot leave unwiped
        // credential fragments in abandoned string allocations.
        headers.value.reserve(headers.value.size()+bytes.size()+32);
        headers.value+=credentialPrefix;
        for(auto byte:bytes) {
            if(byte<33 || byte>126) throw std::invalid_argument("Invalid provider credential");
            headers.value.push_back(static_cast<wchar_t>(byte));
        }
        headers.value+=L"\r\n";
    }
    if(mcp && headers.value.size()>64*1024)throw McpProtocolError("MCP HTTP headers exceed native limits");
    std::array<char,8192> buffer{};State state;
    Handle raw(WinHttpOpenRequest(connection.value,kind==TransferKind::json_get?L"GET":L"POST",parsed.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parsed.secure?WINHTTP_FLAG_SECURE:0));
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;checked(WinHttpSetOption(raw.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)));
    // Never ask the OS to send ambient user credentials to a model endpoint.
    DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;checked(WinHttpSetOption(raw.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)));
    DWORD_PTR context=reinterpret_cast<DWORD_PTR>(&state);
    checked(WinHttpSetOption(raw.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)));
    if(WinHttpSetStatusCallback(raw.value,callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
        throw TransportError("Cannot register native HTTP callback");
    RequestHandle request{raw.release(),state};
    state.prepare();
    if(mcp) {
        if(cancel.stop_requested())throw TransportCancelled("MCP request cancelled before sending");
        if(Clock::now()>=deadline)throw TransportTimeout("MCP request deadline exceeded before sending");
        on_sending();
    }
    checked(WinHttpSendRequest(request.value,headers.value.c_str(),static_cast<DWORD>(headers.value.size()),
        input.body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(input.body.data()),static_cast<DWORD>(input.body.size()),static_cast<DWORD>(input.body.size()),context));
    state.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline,cancel);
    state.prepare();checked(WinHttpReceiveResponse(request.value,nullptr));state.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline,cancel);
    DWORD status=0,size=sizeof(status);
    checked(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
    if(!mcp && (status<200 || status>=300)) {
        ProviderHttpError failure(static_cast<int>(status));
        // Error bodies can contain credentials or user content. Retain only
        // exact, known protocol identifiers; never retain messages or raw JSON.
        if(status>=400) try {
            std::array<wchar_t,256> mime{};DWORD mimeSize=static_cast<DWORD>(sizeof(mime));
            const bool hasMime=WinHttpQueryHeaders(request.value,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,mime.data(),&mimeSize,WINHTTP_NO_HEADER_INDEX)!=FALSE;
            auto diagnosticMedia=hasMime?lower(std::wstring(mime.data())):std::wstring{};
            diagnosticMedia=diagnosticMedia.substr(0,diagnosticMedia.find(L';'));
            while(!diagnosticMedia.empty() && (diagnosticMedia.back()==L' ' || diagnosticMedia.back()==L'\t')) diagnosticMedia.pop_back();
            if(diagnosticMedia==L"application/json") {
                struct Body {std::string value;~Body(){if(!value.empty()) SecureZeroMemory(value.data(),value.size());}} body;
                body.value.reserve(32768);
                const auto diagnosticDeadline=std::min(deadline,Clock::now()+std::chrono::seconds(2));
                for(;;) {
                    state.prepare();checked(WinHttpQueryDataAvailable(request.value,nullptr));
                    const auto available=state.wait(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,diagnosticDeadline,cancel);
                    if(!available) break;
                    if(available>32768-body.value.size()) throw TransportError("Oversized provider diagnostic");
                    state.prepare();checked(WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(std::min<std::size_t>(available,buffer.size())),nullptr));
                    const auto read=state.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,diagnosticDeadline,cancel);
                    if(!read) break;
                    if(read>buffer.size() || read>32768-body.value.size()) throw TransportError("Oversized provider diagnostic");
                    body.value.append(buffer.data(),read);SecureZeroMemory(buffer.data(),read);
                }
                auto parsed=nlohmann::json::parse(body.value,nullptr,false);
                if(parsed.is_object() && parsed.contains("error") && parsed["error"].is_object()) {
                    const auto& error=parsed["error"];
                    auto allowed=[&](const char* key,std::initializer_list<std::string_view> values){
                        if(!error.contains(key)||!error[key].is_string()) return std::string{};
                        const auto& value=error[key].get_ref<const std::string&>();
                        for(auto candidate:values) if(value==candidate) return std::string(candidate);
                        return std::string{};
                    };
                    failure.type=allowed("type",{"invalid_request_error","authentication_error","permission_error","rate_limit_error","server_error","insufficient_quota"});
                    failure.code=allowed("code",{"unsupported_parameter","unsupported_value","invalid_value","missing_required_parameter","model_not_found","invalid_api_key","insufficient_quota","context_length_exceeded","rate_limit_exceeded"});
                    failure.param=allowed("param",{"model","messages","tools","tool_choice","n","stream","stream_options","stream_options.include_usage","max_completion_tokens","max_tokens","temperature","top_p","reasoning_effort","response_format","input","instructions","max_output_tokens"});
                }
            }
        } catch(const TransportCancelled&) {throw;} catch(const std::exception&) {
            // Diagnostic failure must not hide the authoritative HTTP status.
        }
        if(cancel.stop_requested()) throw TransportCancelled("Provider request cancelled");
        throw failure;
    }
    std::array<wchar_t,256> content{};size=static_cast<DWORD>(content.size()*sizeof(wchar_t));
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,content.data(),&size,WINHTTP_NO_HEADER_INDEX)) {
        if(!mcp || GetLastError()!=ERROR_WINHTTP_HEADER_NOT_FOUND)throw TransportError("Missing or oversized HTTP content type");
    }
    auto media=lower(std::wstring(content.data()));media=media.substr(0,media.find(L';'));
    while(!media.empty() && (media.back()==L' ' || media.back()==L'\t')) media.pop_back();
    if(mcp) {
        McpHttpResponseHead head;head.status=static_cast<int>(status);
        for(auto c:media){if(c<32 || c>126)throw TransportError("Invalid MCP response content type");head.media_type+=static_cast<char>(c);}
        head.legacy_session=response_field(request.value,L"Mcp-Session-Id");head.authenticate=response_field(request.value,L"WWW-Authenticate",true);
        on_head(head);
        if(status==200 && media!=L"application/json" && media!=L"text/event-stream")throw TransportError("Unexpected MCP response content type");
    } else if(media!=(json?L"application/json":L"text/event-stream")) throw TransportError("Unexpected provider response content type");
    const std::size_t limit=response_limit;
    std::size_t received=0;
    for(;;) {
        if(cancel.stop_requested()) throw TransportCancelled("Provider request cancelled");
        state.prepare();checked(WinHttpQueryDataAvailable(request.value,nullptr));
        const auto available=state.wait(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,deadline,cancel);if(!available) break;
        if(mcp && status==202)throw TransportError("MCP notification acknowledgement must have an empty body");
        state.prepare();checked(WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(std::min<std::size_t>(available,buffer.size())),nullptr));
        const auto read=state.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline,cancel);if(!read) break;
        if(read>buffer.size() || read>limit-received) throw TransportError("Provider response exceeds configured limit");
        received+=read;consume(std::string_view(buffer.data(),read));
    }
}
void post_event_stream(const HttpStreamRequest& input,const SecretBytes* bearer,
    const std::function<void(std::string_view)>& consume,std::stop_token cancel) {
    transfer(input,bearer,consume,cancel,TransferKind::event_post,64*1024*1024);
}
std::string get_json(const HttpStreamRequest& input,const SecretBytes* bearer,std::stop_token cancel) {
    if(!input.body.empty()) throw std::invalid_argument("JSON discovery cannot send a request body");
    std::string result;
    transfer(input,bearer,[&](std::string_view chunk){result.append(chunk);},cancel,TransferKind::json_get,1024*1024);
    return result;
}
std::string post_json(const HttpStreamRequest& input,const SecretBytes* bearer,
    std::size_t max_response_bytes,std::stop_token cancel) {
    if(input.body.empty() || max_response_bytes==0 || max_response_bytes>8*1024*1024)
        throw std::invalid_argument("Invalid bounded JSON provider POST");
    std::string result;
    transfer(input,bearer,[&](std::string_view chunk){result.append(chunk);},cancel,TransferKind::json_post,max_response_bytes);
    return result;
}
void validate_mcp_http_endpoint(std::string_view value) {(void)endpoint(std::string(value));}
void post_mcp_http(const McpHttpPost& input,const SecretBytes* bearer,
    const std::function<void(const McpHttpResponseHead&)>& on_head,
    const std::function<void(std::string_view)>& consume,
    const std::function<void()>& on_sending,std::stop_token cancel) {
    if(input.body.empty() || !on_head || !on_sending)throw std::invalid_argument("MCP HTTP requires body and ownership callbacks");
    const HttpStreamRequest request{input.url,input.body,input.deadline,input.idle_timeout};
    transfer(request,bearer,consume,cancel,TransferKind::mcp_post,8*1024*1024,&input,on_head,on_sending);
}
}
