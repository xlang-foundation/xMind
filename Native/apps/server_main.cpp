#include "agentflow/http_server.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
namespace {
std::mutex control_mutex;
agentflow::HttpServer* active_server=nullptr;
BOOL WINAPI control(DWORD event) {
    if(event!=CTRL_C_EVENT && event!=CTRL_BREAK_EVENT) return FALSE;
    std::lock_guard lock(control_mutex);
    if(active_server) active_server->stop();return TRUE;
}
}
#endif
int main(int argc,char** argv) {
    try {
        std::map<std::string,std::string> options;
        for(int i=1;i<argc;i+=2) {
            const std::string key=argv[i];
            if(i+1>=argc || (key!="--db" && key!="--modules" && key!="--stdlib" && key!="--port") || !options.emplace(key,argv[i+1]).second)
                throw std::invalid_argument("Usage: xmind_server --db FILE --modules DIR --stdlib DIR [--port PORT]");
        }
        for(const auto* key:{"--db","--modules","--stdlib"}) if(!options.contains(key)) throw std::invalid_argument("Missing server configuration");
        int port=8765;
        if(options.contains("--port")) {
            const auto& value=options.at("--port");const auto parsed=std::from_chars(value.data(),value.data()+value.size(),port);
            if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid port");
        }
        const auto* token=std::getenv("XMIND_AUTH_TOKEN");
        if(!token) throw std::invalid_argument("Set XMIND_AUTH_TOKEN for the local server");
        agentflow::validate_local_auth_token(token);
        if(port<0 || port>65535) throw std::invalid_argument("Invalid port");
        agentflow::PersistenceService persistence(options.at("--db"),{options.at("--modules"),options.at("--stdlib")});
        agentflow::HttpServer server(persistence,token);const auto bound=server.bind(port);
#if defined(_WIN32)
        {std::lock_guard lock(control_mutex);active_server=&server;}
        SetConsoleCtrlHandler(control,TRUE);
#endif
        std::cout<<"xMind Server listening on http://127.0.0.1:"<<bound<<std::endl;
        const auto ok=server.listen();
#if defined(_WIN32)
        {std::lock_guard lock(control_mutex);active_server=nullptr;}
        SetConsoleCtrlHandler(control,FALSE);
#endif
        return ok?0:1;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
