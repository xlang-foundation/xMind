#pragma once
#include "httplib.h"
#include <regex>
#include <vector>

namespace agentflow {
// One route inventory for loopback HTTP and bound, in-process requests. The
// latter never opens a socket and retains the same admission/error handlers.
class NativeRequestRouter : public httplib::Server {
    struct Route { std::string method; std::regex pattern; Handler handler; };
    std::vector<Route> routes_;
    HandlerWithResponse admission_;
    HandlerWithResponse error_;
    ExceptionHandler exception_;
public:
    NativeRequestRouter& Get(const std::string& path, Handler handler) {
        routes_.push_back({"GET",std::regex(path),handler});
        httplib::Server::Get(path,std::move(handler));return *this;
    }
    NativeRequestRouter& Post(const std::string& path, Handler handler) {
        routes_.push_back({"POST",std::regex(path),handler});
        httplib::Server::Post(path,std::move(handler));return *this;
    }
    NativeRequestRouter& Delete(const std::string& path, Handler handler) {
        routes_.push_back({"DELETE",std::regex(path),handler});
        httplib::Server::Delete(path,std::move(handler));return *this;
    }
    NativeRequestRouter& set_pre_routing_handler(HandlerWithResponse handler) {
        admission_=handler;httplib::Server::set_pre_routing_handler(std::move(handler));return *this;
    }
    NativeRequestRouter& set_error_handler(Handler handler) {
        error_=[handler](const auto& request,auto& response){handler(request,response);return HandlerResponse::Handled;};
        httplib::Server::set_error_handler(std::move(handler));return *this;
    }
    NativeRequestRouter& set_exception_handler(ExceptionHandler handler) {
        exception_=handler;httplib::Server::set_exception_handler(std::move(handler));return *this;
    }
    void dispatch(httplib::Request& request,httplib::Response& response) {
        try {
            if(admission_&&admission_(request,response)==HandlerResponse::Handled)return;
            for(const auto& route:routes_) {
                if(route.method==request.method&&std::regex_match(request.path,request.matches,route.pattern)) {
                    route.handler(request,response);if(response.status<0)response.status=200;return;
                }
            }
            response.status=404;if(error_)error_(request,response);
        } catch(...) {
            if(exception_)exception_(request,response,std::current_exception());
            else throw;
        }
    }
};
}
