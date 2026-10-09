#pragma once
#include "httplib.h"
#include <memory>
#include <string>
#if defined(_WIN32)
#include "agentflow/workspace_tools.hpp"
#endif
namespace agentflow {
// Client-side connection pinning. The backend still owns admission and effects.
// No persistence or agent execution occurs in this transport.
class ConsoleTransport {
public:
    ConsoleTransport(int port,const std::string& workspace);
    httplib::Result Get(const std::string& route,const httplib::Headers& headers);
    httplib::Result Post(const std::string& route,const httplib::Headers& headers,
        const std::string& body,const std::string& content_type);
    void set_connection_timeout(int seconds,int micros){client_.set_connection_timeout(seconds,micros);}
    void set_read_timeout(int seconds,int micros){client_.set_read_timeout(seconds,micros);}
    void set_write_timeout(int seconds,int micros){client_.set_write_timeout(seconds,micros);}
    void set_follow_location(bool value){client_.set_follow_location(value);}
private:
    void verify(const httplib::Headers& headers);
    httplib::Client client_;
#if defined(_WIN32)
    std::unique_ptr<WorkspaceTools> workspace_;
#endif
    std::string workspace_id_,root_,authority_;
};
}
