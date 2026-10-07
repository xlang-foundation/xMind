#include "agentflow/http_stream_transport.hpp"
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cwctype>
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
}
void post_event_stream(const HttpStreamRequest& input,const SecretBytes* bearer,
    const std::function<void(std::string_view)>& consume,std::stop_token cancel) {
    if(!consume || input.url.empty() || input.url.size()>8192 || input.body.size()>8*1024*1024 ||
        input.deadline.count()<=0 || input.deadline.count()>600000 || input.idle_timeout.count()<=0 || input.idle_timeout.count()>600000)
        throw std::invalid_argument("Invalid provider transport configuration");
    for(unsigned char c:input.url) if(c<=32 || c==127 || c=='#') throw std::invalid_argument("Invalid endpoint URL");
    if(cancel.stop_requested()) throw TransportCancelled("Provider request cancelled");
    const auto deadline=Clock::now()+input.deadline;
    const auto url=wide(input.url);
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts)) throw std::invalid_argument("Invalid endpoint URL");
    if(parts.dwUserNameLength || parts.dwPasswordLength || input.url.find('@')!=std::string::npos) throw std::invalid_argument("Endpoint cannot contain credentials");
    const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    const auto secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
    const auto normalized=lower(host);
    if(!secure && (parts.nScheme!=INTERNET_SCHEME_HTTP || (normalized!=L"localhost" && normalized!=L"127.0.0.1" && normalized!=L"::1")))
        throw std::invalid_argument("Remote provider endpoints require HTTPS");
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);if(path.empty()) path=L"/";
    if(parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    Handle session(WinHttpOpen(L"xMind/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
    checked(WinHttpSetTimeouts(session.value,10000,10000,10000,static_cast<int>(input.idle_timeout.count())));
    Handle connection(WinHttpConnect(session.value,host.c_str(),parts.nPort,0));
    WipedHeaders headers;
    headers.value=L"Content-Type: application/json\r\nAccept: text/event-stream\r\n";
    if(bearer) {
        const auto bytes=bearer->view();
        if(bytes.empty() || bytes.size()>16384) throw std::invalid_argument("Invalid provider credential");
        // Reserve before copying secret bytes so growth cannot leave unwiped
        // credential fragments in abandoned string allocations.
        headers.value.reserve(headers.value.size()+bytes.size()+32);
        headers.value+=L"Authorization: Bearer ";
        for(auto byte:bytes) {
            if(byte<33 || byte>126) throw std::invalid_argument("Invalid provider credential");
            headers.value.push_back(static_cast<wchar_t>(byte));
        }
        headers.value+=L"\r\n";
    }
    std::array<char,8192> buffer{};State state;
    Handle raw(WinHttpOpenRequest(connection.value,L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,secure?WINHTTP_FLAG_SECURE:0));
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;checked(WinHttpSetOption(raw.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)));
    // Never ask the OS to send ambient user credentials to a model endpoint.
    DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;checked(WinHttpSetOption(raw.value,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)));
    DWORD_PTR context=reinterpret_cast<DWORD_PTR>(&state);
    checked(WinHttpSetOption(raw.value,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)));
    if(WinHttpSetStatusCallback(raw.value,callback,WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
        throw TransportError("Cannot register native HTTP callback");
    RequestHandle request{raw.release(),state};
    state.prepare();
    checked(WinHttpSendRequest(request.value,headers.value.c_str(),static_cast<DWORD>(headers.value.size()),
        input.body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(input.body.data()),static_cast<DWORD>(input.body.size()),static_cast<DWORD>(input.body.size()),context));
    state.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline,cancel);
    state.prepare();checked(WinHttpReceiveResponse(request.value,nullptr));state.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline,cancel);
    DWORD status=0,size=sizeof(status);
    checked(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX));
    if(status<200 || status>=300) throw ProviderHttpError(static_cast<int>(status));
    std::array<wchar_t,256> content{};size=static_cast<DWORD>(content.size()*sizeof(wchar_t));
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,content.data(),&size,WINHTTP_NO_HEADER_INDEX)) throw TransportError("Missing or oversized provider content type");
    auto media=lower(std::wstring(content.data()));media=media.substr(0,media.find(L';'));
    while(!media.empty() && (media.back()==L' ' || media.back()==L'\t')) media.pop_back();
    if(media!=L"text/event-stream") throw TransportError("Provider response is not an event stream");
    std::size_t received=0;
    for(;;) {
        if(cancel.stop_requested()) throw TransportCancelled("Provider request cancelled");
        state.prepare();checked(WinHttpQueryDataAvailable(request.value,nullptr));
        const auto available=state.wait(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,deadline,cancel);if(!available) break;
        state.prepare();checked(WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(std::min<std::size_t>(available,buffer.size())),nullptr));
        const auto read=state.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline,cancel);if(!read) break;
        if(read>buffer.size() || read>64*1024*1024-received) throw TransportError("Provider response exceeds configured limit");
        received+=read;consume(std::string_view(buffer.data(),read));
    }
}
}
