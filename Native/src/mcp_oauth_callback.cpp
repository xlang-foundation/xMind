#include "agentflow/mcp_oauth_callback.hpp"
#include "agentflow/mcp_wire.hpp"
#include "httplib.h"
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

namespace agentflow {
namespace {
void page(httplib::Response& response,int status,bool accepted) {
    response.status=status;
    response.set_content(accepted?
        "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>xMind authorization</title><p>Authorization received. Return to xMind to finish connecting.</p></html>":
        "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>xMind authorization</title><p>Authorization was not received. Return to xMind.</p></html>","text/html; charset=utf-8");
}
}
struct McpOAuthLoopbackCallback::Impl {
    httplib::Server server;
    std::string path,host,uri;
    bool consumed=false;
    explicit Impl(std::string value,std::uint16_t requested):path(std::move(value)) {
        if(path.empty()||path.size()>128||path.front()!='/')throw std::invalid_argument("Invalid native OAuth callback path");
        for(unsigned char c:path)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-'))throw std::invalid_argument("Invalid native OAuth callback path");
        server.new_task_queue=[] {return new httplib::ThreadPool(1,1,4);};
        server.set_socket_options([](socket_t socket){
            // Windows SO_REUSEADDR permits another process to steal a bound
            // callback port. Require exclusive binding before advertising it.
            const BOOL exclusive=TRUE;
            if(setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==SOCKET_ERROR) {
                closesocket(socket);throw McpTransportError("Cannot protect native OAuth callback socket");
            }
        });
        server.set_read_timeout(1,0);server.set_write_timeout(1,0);
        server.set_keep_alive_max_count(1);server.set_payload_max_length(0);
        server.set_default_headers({{"Cache-Control","no-store"},{"Referrer-Policy","no-referrer"},{"X-Content-Type-Options","nosniff"},{"Content-Security-Policy","default-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'"}});
        int port=requested;
        if(requested){if(!server.bind_to_port("127.0.0.1",requested))throw McpTransportError("Cannot bind native OAuth callback port");}
        else port=server.bind_to_any_port("127.0.0.1");
        if(port<=0)throw McpTransportError("Cannot bind native OAuth callback port");
        host="127.0.0.1:"+std::to_string(port);uri="http://"+host+path;
    }
    ~Impl(){server.stop();}
};
McpOAuthLoopbackCallback::McpOAuthLoopbackCallback(std::string path,std::uint16_t port):impl_(std::make_unique<Impl>(std::move(path),port)){}
McpOAuthLoopbackCallback::~McpOAuthLoopbackCallback()=default;
std::string McpOAuthLoopbackCallback::redirect_uri() const {return impl_->uri;}
void McpOAuthLoopbackCallback::receive(McpOAuthAuthorizationAttempt& attempt,McpDeadline deadline,std::stop_token cancel) {
    auto& owner=*impl_;
    if(owner.consumed)throw McpProtocolError("Native OAuth callback receiver was already used");
    owner.consumed=true;
    if(cancel.stop_requested()){owner.server.stop();attempt.cancel();throw McpTransportCancelled("MCP OAuth callback cancelled");}
    // Ensure the attempt is still awaiting a callback, before starting threads.
    try{
        deadline=std::min(deadline,attempt.expires_at());
        if(attempt.redirect_uri()!=owner.uri)throw McpProtocolError("Native OAuth callback redirect differs");
        if(deadline<=std::chrono::steady_clock::now())throw McpTransportTimeout("MCP OAuth callback deadline exceeded");
        (void)attempt.authorization_url();
    }catch(...){owner.server.stop();attempt.cancel();throw;}
    std::mutex mutex;std::condition_variable changed;bool completed=false;std::exception_ptr failure;
    owner.server.set_pre_routing_handler([&](const httplib::Request& request,httplib::Response& response){
        if(request.get_header_value_count("Host")!=1||request.get_header_value("Host")!=owner.host||request.has_header("Origin")||request.has_header("Authorization")||request.target.size()>8192) {
            page(response,400,false);return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    owner.server.set_error_handler([](const httplib::Request&,httplib::Response& response){page(response,response.status,false);});
    owner.server.Get(owner.path,[&](const httplib::Request& request,httplib::Response& response){
        std::lock_guard lock(mutex);
        if(completed){page(response,409,false);return;}
        try {
            if(cancel.stop_requested())throw McpTransportCancelled("MCP OAuth callback cancelled");
            if(std::chrono::steady_clock::now()>=deadline)throw McpTransportTimeout("MCP OAuth callback deadline exceeded");
            const auto query=request.target.find('?');
            if(query==std::string::npos||request.target.substr(0,query)!=owner.path)throw McpProtocolError("Invalid native OAuth callback target");
            attempt.accept_callback(owner.uri,std::string_view(request.target).substr(query+1));
            page(response,200,true);
        }catch(...){failure=std::current_exception();page(response,400,false);}
        completed=true;changed.notify_one();
    });
    std::jthread listener([&]{
        try {
            if(!owner.server.listen_after_bind()){
                std::lock_guard lock(mutex);if(!completed){failure=std::make_exception_ptr(McpTransportError("Native OAuth callback listener failed"));completed=true;changed.notify_one();}
            }
        }catch(...){std::lock_guard lock(mutex);if(!completed){failure=std::make_exception_ptr(McpTransportError("Native OAuth callback listener failed"));completed=true;changed.notify_one();}}
    });
    std::stop_callback wake(cancel,[&]{std::lock_guard lock(mutex);changed.notify_one();});
    bool cancelled=false,timed_out=false;
    {
        std::unique_lock lock(mutex);
        changed.wait_until(lock,deadline,[&]{return completed||cancel.stop_requested();});
        cancelled=cancel.stop_requested();timed_out=!completed&&!cancelled;
    }
    owner.server.stop();listener.join();
    if(cancelled){attempt.cancel();throw McpTransportCancelled("MCP OAuth callback cancelled");}
    if(timed_out){attempt.cancel();throw McpTransportTimeout("MCP OAuth callback deadline exceeded");}
    if(failure){attempt.cancel();std::rethrow_exception(failure);}
    if(!attempt.ready()){attempt.cancel();throw McpProtocolError("Native OAuth callback did not authorize an exchange");}
}
}
