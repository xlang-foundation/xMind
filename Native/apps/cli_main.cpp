#include "httplib.h"
#include "nlohmann/json.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <thread>
#include <limits>

namespace {
std::int64_t event_cursor(const std::string& source) {
    std::int64_t value=0;const auto parsed=std::from_chars(source.data(),source.data()+source.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=source.data()+source.size() || value<0)throw std::invalid_argument("Invalid cursor");
    return value;
}
// Observation only: ending this client never grants, cancels or owns execution.
// Each flushed NDJSON record is an actual persisted backend event. Its seq can
// be supplied on reconnect; process-output hex is never written as terminal code.
int watch_run(httplib::Client& client,const httplib::Headers& headers,const std::string& run,std::int64_t cursor) {
    using Json=nlohmann::json;const auto path="/v1/runs/"+run;
    auto read=[&](const std::string& route) {
        auto response=client.Get(route,headers);if(!response)throw std::runtime_error("Cannot reach xMind Server during observation");
        if(response->status<200 || response->status>=300)throw std::runtime_error("Server rejected run observation (HTTP "+std::to_string(response->status)+")");
        return Json::parse(response->body);
    };
    auto emit=[&] {
        const auto events=read(path+"/events?after="+std::to_string(cursor));
        if(!events.is_array())throw std::runtime_error("Invalid backend event batch");
        for(const auto& event:events) {
            if(!event.is_object() || !event.contains("seq") || !event["seq"].is_number_integer() || event["seq"]<=cursor || event["seq"]>std::numeric_limits<std::int64_t>::max() || !event.contains("run_id") || event["run_id"]!=run || !event.contains("kind") || !event["kind"].is_string() || !event.contains("data"))throw std::runtime_error("Invalid backend event identity or cursor");
            std::cout<<event.dump()<<'\n'<<std::flush;
            if(!std::cout)throw std::runtime_error("Run observation output is unavailable");
            cursor=event["seq"].get<std::int64_t>();
        }
    };
    for(;;) {
        emit();const auto state=read(path);
        if(!state.is_object() || state.value("id",std::string{})!=run || !state.contains("state") || !state["state"].is_string())throw std::runtime_error("Invalid backend run identity or state");
        const auto value=state["state"].get<std::string>();
        if(value=="completed" || value=="failed" || value=="cancelled") {
            // A transition can commit between the event query and status query.
            emit();return value=="completed"?0:value=="cancelled"?2:1;
        }
        if(value!="queued" && value!="running" && value!="paused")throw std::runtime_error("Unknown backend run state");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}
}

int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::invalid_argument("Usage: xmind_cli PORT COMMAND [ARGS] (commands: health, sessions, create-session, history, runs, run, cancel, status, events, watch, models, instructions, mcp-servers, process-profiles, operations, operation, inspect-edit, decide, append-message)");
        const std::string port_text=argv[1],command=argv[2];int port=0;
        const auto parsed=std::from_chars(port_text.data(),port_text.data()+port_text.size(),port);
        if(parsed.ec!=std::errc{} || parsed.ptr!=port_text.data()+port_text.size() || port<1 || port>65535) throw std::invalid_argument("Invalid port");
        auto id=[](const std::string& value) {
            if(value.empty() || value.size()>128) throw std::invalid_argument("Invalid ID");
            for(unsigned char c:value) if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) throw std::invalid_argument("Invalid ID");
            return value;
        };
        using Json=nlohmann::json;std::string path;Json body;bool post=false,watch=false;std::int64_t watch_cursor=0;
        if(command=="health" && argc==3) path="/v1/health";
        else if(command=="sessions" && argc==3) path="/v1/sessions";
        else if(command=="create-session" && argc==4) {path="/v1/sessions";body={{"title",argv[3]}};post=true;}
        else if(command=="history" && argc==4) path="/v1/sessions/"+id(argv[3])+"/history";
        else if(command=="runs" && argc==4) path="/v1/sessions/"+id(argv[3])+"/runs";
        else if(command=="status" && argc==4) path="/v1/runs/"+id(argv[3]);
        else if(command=="operations" && argc==4) path="/v1/runs/"+id(argv[3])+"/operations";
        else if(command=="operation" && argc==4) path="/v1/operations/"+id(argv[3]);
        else if(command=="inspect-edit" && argc==4) path="/v1/operations/"+id(argv[3])+"/inspection";
        else if(command=="decide" && argc==5) {
            const std::string decision=argv[4];
            if(decision!="allow" && decision!="deny") throw std::invalid_argument("Decision must be allow or deny");
            path="/v1/operations/"+id(argv[3])+"/decision";body={{"decision",decision}};post=true;
        }
        else if(command=="models" && argc==3) path="/v1/models";
        else if(command=="mcp-servers" && argc==3) path="/v1/mcp/servers";
        else if(command=="process-profiles" && argc==3) path="/v1/process/profiles";
        else if(command=="instructions" && argc==3) path="/v1/agent/instructions";
        else if(command=="run" && (argc==5 || argc==6)) {path="/v1/runs";body={{"session_id",id(argv[3])},{"prompt",argv[4]}};if(argc==6)body["model_id"]=argv[5];post=true;}
        else if(command=="cancel" && argc==4) {path="/v1/runs/"+id(argv[3])+"/cancel";body=Json::object();post=true;}
        else if(command=="watch" && (argc==4 || argc==5)) {path=id(argv[3]);watch=true;watch_cursor=event_cursor(argc==5?argv[4]:"0");}
        else if(command=="events" && (argc==4 || argc==5)) {
            const std::string cursor=argc==5?argv[4]:"0";event_cursor(cursor);
            path="/v1/runs/"+id(argv[3])+"/events?after="+cursor;
        }
        else if(command=="append-message" && argc==5) {path="/v1/sessions/"+id(argv[3])+"/messages";body={{"role","user"},{"data",{{"content",argv[4]}}}};post=true;}
        else throw std::invalid_argument("Unknown command or incorrect arguments");
        const auto* token=std::getenv("XMIND_AUTH_TOKEN");
        if(!token) throw std::invalid_argument("Set XMIND_AUTH_TOKEN for the local client");
        httplib::Client client("127.0.0.1",port);
        client.set_connection_timeout(5,0);client.set_read_timeout(15,0);client.set_write_timeout(5,0);client.set_follow_location(false);
        const httplib::Headers headers{{"Authorization",std::string("Bearer ")+token}};
        if(watch)return watch_run(client,headers,path,watch_cursor);
        auto response=post?client.Post(path,headers,body.dump(),"application/json"):client.Get(path,headers);
        if(!response) throw std::runtime_error("Cannot reach xMind Server");
        const auto result=Json::parse(response->body);
        if(response->status<200 || response->status>=300) {std::cerr<<result.dump()<<'\n';return 1;}
        std::cout<<result.dump(2)<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
