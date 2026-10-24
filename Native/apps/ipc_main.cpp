#include "xlang3/xlang3.h"
#include "nlohmann/json.hpp"
#include "../src/agent_worker_sdk_values.hpp"
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace agentflow {
int run_xmind_ipc_client(int argc,char** argv){
    try{
        if(argc<4||std::string(argv[1])!="--endpoint")throw std::invalid_argument("Usage: xmind ipc --endpoint lrpc:PORT host | connections list | connections save REV JSON_ARRAY | connections select ID REV | connections open ID REV | connections session-create ID REV SESSION TITLE | connections session-open ID REV SESSION | connections messages ID REV SESSION BEFORE_SEQUENCE COUNT | connections run-submit ID REV JSON_OBJECT | connections run-start ID REV RUN SESSION PROMPT | connections run-open ID REV RUN | connections run-history ID REV SESSION RUN BEFORE_SEQUENCE COUNT | connections run-cancel ID REV RUN | connections operations ID REV SESSION RUN | connections operation ID REV SESSION RUN OPERATION | connections operation-decide ID REV SESSION RUN OPERATION allow|deny | connections graphs ID REV | connections graph-children ID REV ROOT | connections graph-open ID REV ROOT | connections graph-submit ID REV JSON_OBJECT | connections graph-input ID REV ROOT NODE JSON_INPUT CHECKPOINT_REV | connections graph-tree-input ID REV ROOT OWNER NODE JSON_INPUT ROOT_REV OWNER_REV | connections graph-resume ID REV ROOT CHECKPOINT_REV | connections workspace-files ID REV FOLDER MEMBERSHIP_REV PATH | connections workspace-file ID REV FOLDER MEMBERSHIP_REV PATH | connections workspace-find ID REV FOLDER MEMBERSHIP_REV JSON_OBJECT | connections workspace-info ID REV | connections workspace-membership ID REV | connections commands ID REV | connections command-invoke ID REV SESSION JSON_OBJECT | connections agents ID REV | connections agent-definition ID REV AGENT | connections agent-open ID REV SESSION | connections agent-select ID REV SESSION JSON_OBJECT | connections models ID REV | connections model-open ID REV SESSION | connections model-select ID REV SESSION JSON_OBJECT | connections run-events ID REV SESSION RUN JSON_CURSOR | connections events ID REV SESSION RUN JSON_CURSOR | session create ID TITLE | session open ID | run open ID | run start ID SESSION PROMPT | run cancel ID");
        const std::string endpoint=argv[2];if(!endpoint.starts_with("lrpc:"))throw std::invalid_argument("xMind IPC requires an xlang3 LRPC endpoint");
        X::Runtime runtime;auto root=runtime.ImportRemote("xmind",endpoint.c_str());
        auto call=[&](const X::Value& method,std::vector<X::Value> args){X::Value result;if(!method.Call(args,result))throw std::runtime_error("xMind IPC call failed: "+runtime.LastError());return result;};
        auto text=[&](const char* value){return X::Value(runtime,std::string(value));};
        const std::string command=argv[3];nlohmann::json output;
        if(command=="host"&&argc==4){auto host=call(root["Host"],{});output={{"api_version",call(host["ApiVersion"],{}).ToInt64()},{"transport",call(host["Transport"],{}).ToString()}};}
        else{
            const auto* token=std::getenv("XMIND_AUTH_TOKEN");if(!token)throw std::invalid_argument("Set XMIND_AUTH_TOKEN for authenticated xMind IPC services");
            if(command=="connections"){
                auto service=call(root["Connections"],{text(token)});X::Value result;
                const auto number=[](const char* input,const char* field){const std::string value=input;std::int64_t number=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||number<0||number>9007199254740991LL)throw std::invalid_argument(std::string("Invalid ")+field);return X::Value(number);};
                const auto revision=[&](const char* input){return number(input,"connection revision");};
                if(argc==5&&std::string(argv[4])=="list")result=call(service["List"],{});
                else if(argc==7&&std::string(argv[4])=="save")result=call(service["Save"],{sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[6])),revision(argv[5])});
                else if(argc==7&&std::string(argv[4])=="select")result=call(service["Select"],{text(argv[5]),revision(argv[6])});
                else if(argc>=7){
                    auto binding=call(service["Open"],{text(argv[5]),revision(argv[6])});const std::string method=argv[4];
                    if(method=="open"&&argc==7)result=call(binding["Identity"],{});
                    else if(method=="session-create"&&argc==9){auto sessions=call(binding["Sessions"],{});result=call(sessions["Create"],{text(argv[7]),text(argv[8])});}
                    else if(method=="session-open"&&argc==8){auto sessions=call(binding["Sessions"],{});result=call(sessions["Open"],{text(argv[7])});}
                    else if(method=="messages"&&argc==10){auto sessions=call(binding["Sessions"],{});result=call(sessions["Messages"],{text(argv[7]),number(argv[8],"history cursor"),number(argv[9],"history count")});}
                    else if(method=="graphs"&&argc==7){auto graphs=call(binding["Graphs"],{});result=call(graphs["List"],{});}
                    else if(method=="graph-children"&&argc==8){auto graphs=call(binding["Graphs"],{});result=call(graphs["Children"],{text(argv[7])});}
                    else if(method=="graph-open"&&argc==8){auto graphs=call(binding["Graphs"],{});result=call(graphs["Open"],{text(argv[7])});}
                    else if(method=="graph-submit"&&argc==8){auto graphs=call(binding["Graphs"],{});result=call(graphs["Submit"],{sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[7]))});}
                    else if(method=="graph-input"&&argc==11){auto graphs=call(binding["Graphs"],{});result=call(graphs["Input"],{text(argv[7]),text(argv[8]),text(argv[9]),number(argv[10],"checkpoint revision")});}
                    else if(method=="graph-tree-input"&&argc==13){auto graphs=call(binding["Graphs"],{});result=call(graphs["TreeInput"],{text(argv[7]),text(argv[8]),text(argv[9]),text(argv[10]),number(argv[11],"root checkpoint revision"),number(argv[12],"owner checkpoint revision")});}
                    else if(method=="graph-resume"&&argc==9){auto graphs=call(binding["Graphs"],{});result=call(graphs["Resume"],{text(argv[7]),number(argv[8],"checkpoint revision")});}
                    else if(method=="run-submit"&&argc==8){auto runs=call(binding["Runs"],{});result=call(runs["Submit"],{sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[7]))});}
                    else if(method=="run-history"&&argc==11){auto runs=call(binding["Runs"],{});result=call(runs["History"],{text(argv[7]),text(argv[8]),number(argv[9],"run history cursor"),number(argv[10],"run history count")});}
                    else if(method=="run-open"&&argc==8){auto runs=call(binding["Runs"],{});result=call(runs["Open"],{text(argv[7])});}
                    else if(method=="run-start"&&argc==10){auto runs=call(binding["Runs"],{});result=call(runs["Start"],{text(argv[7]),text(argv[8]),text(argv[9])});}
                    else if(method=="run-cancel"&&argc==8){auto runs=call(binding["Runs"],{});call(runs["Cancel"],{text(argv[7])});result=runtime.Dict();result.Set("binding",call(binding["Identity"],{}));result.Set("run_id",text(argv[7]));result.Set("cancel_requested",X::Value(true));}
                    else if((method=="operations"&&argc==9)||(method=="operation"&&argc==10)||(method=="operation-decide"&&argc==11)){auto operations=call(binding["Operations"],{text(argv[7]),text(argv[8])});if(method=="operations")result=call(operations["List"],{});else if(method=="operation")result=call(operations["Open"],{text(argv[9])});else result=call(operations["Decide"],{text(argv[9]),text(argv[10])});}
                    else if((method=="workspace-files"||method=="workspace-file"||method=="workspace-find")&&argc==10){auto workspace=call(binding["Workspace"],{});result=call(workspace[method=="workspace-files"?"Files":method=="workspace-file"?"File":"Find"],{text(argv[7]),number(argv[8],"workspace membership revision"),method=="workspace-find"?sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[9])):text(argv[9])});}
                    else if((method=="workspace-info"||method=="workspace-membership")&&argc==7){auto workspace=call(binding["Workspace"],{});result=call(workspace[method=="workspace-info"?"Info":"Membership"],{});}
                    else if((method=="commands"&&argc==7)||(method=="command-invoke"&&argc==9)){auto commands=call(binding["Commands"],{});if(method=="commands")result=call(commands["List"],{});else result=call(commands["Invoke"],{text(argv[7]),sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[8]))});}
                    else if((method=="model-open"&&argc==8)||(method=="model-select"&&argc==9)){auto models=call(binding["Models"],{});if(method=="model-open")result=call(models["Open"],{text(argv[7])});else result=call(models["Select"],{text(argv[7]),sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[8]))});}
                    else if((method=="permissions"||method=="permission-grants")&&argc==8){auto permissions=call(binding["Permissions"],{text(argv[7])});result=call(permissions[method=="permissions"?"Pending":"Grants"],{});}
                    else if(method=="permission-reply"&&argc==10){auto permissions=call(binding["Permissions"],{text(argv[7])});result=call(permissions["Reply"],{text(argv[8]),sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[9]))});}
                    else if(method=="permission-revoke"&&argc==10){auto permissions=call(binding["Permissions"],{text(argv[7])});result=call(permissions["Revoke"],{text(argv[8]),number(argv[9],"permission grant revision")});}
                    else if((method=="provider-save"||method=="provider-models"||method=="provider-import")&&argc==8){auto providers=call(binding["Providers"],{});result=call(providers[method=="provider-save"?"Save":method=="provider-models"?"Discover":"Import"],{sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[7]))});}
                    else if((method=="providers"&&argc==7)||(method=="provider-select"&&argc==9)){auto providers=call(binding["Providers"],{});result=method=="providers"?call(providers["List"],{}):call(providers["Select"],{text(argv[7]),number(argv[8],"provider profile revision")});}
                    else if((method=="skills"&&argc==7)||(method=="skills-open"&&argc==8)||(method=="skills-select"&&argc==9)){auto service=call(binding["Skills"],{});if(method=="skills")result=call(service["List"],{});else if(method=="skills-open")result=call(service["Open"],{text(argv[7])});else result=call(service["Select"],{text(argv[7]),sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[8]))});}
                    else if((method=="context-open"&&argc==9)||(method=="context-request"&&argc==10)||(method=="context-compact"&&argc==9)){auto service=call(binding["Contexts"],{});if(method=="context-open")result=call(service["Open"],{text(argv[7]),text(argv[8])});else if(method=="context-request")result=call(service["Request"],{text(argv[7]),text(argv[8]),text(argv[9])});else result=call(service["Compact"],{text(argv[7]),sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[8]))});}
                    else if((method=="agents"&&argc==7)||(method=="agent-definition"&&argc==8)||(method=="agent-open"&&argc==8)||(method=="agent-select"&&argc==9)){auto agents=call(binding["Agents"],{});if(method=="agents")result=call(agents["List"],{});else if(method=="agent-definition")result=call(agents["Definition"],{text(argv[7])});else if(method=="agent-open")result=call(agents["Open"],{text(argv[7])});else result=call(agents["Select"],{text(argv[7]),sdk_values::to_value(runtime.host(),nlohmann::json::parse(argv[8]))});}
                    else if(method=="models"&&argc==7){auto models=call(binding["Models"],{});result=call(models["List"],{});}
                    else if((method=="events"||method=="run-events")&&argc==10){auto events=call(binding[method=="run-events"?"RunEvents":"Events"],{text(argv[7]),text(argv[8])});const auto input=nlohmann::json::parse(argv[9]);auto cursor=input.is_null()?call(events["Cursor"],{}):sdk_values::to_value(runtime.host(),input);result=call(events["Read"],{cursor});}
                    else throw std::invalid_argument("Invalid captured connection method or arguments");
                }
                else throw std::invalid_argument("Invalid connection IPC method or arguments");
                output=sdk_values::from_value(result);std::cout<<output.dump()<<std::endl;return 0;
            }
            if(command!="session"&&command!="run")throw std::invalid_argument("Unknown xMind IPC service");auto service=call(root[command=="session"?"Sessions":"Runs"],{text(token)});X::Value result;
            if(command=="session"&&argc==7&&std::string(argv[4])=="create")result=call(service["Create"],{text(argv[5]),text(argv[6])});
            else if(argc==6&&std::string(argv[4])=="open")result=call(service["Open"],{text(argv[5])});
            else if(command=="run"&&argc==8&&std::string(argv[4])=="start")result=call(service["Start"],{text(argv[5]),text(argv[6]),text(argv[7])});
            else if(command=="run"&&argc==6&&std::string(argv[4])=="cancel"){call(service["Cancel"],{text(argv[5])});output={{"cancel_requested",true}};}
            else throw std::invalid_argument("Invalid xMind IPC service method or arguments");
            if(result.IsValid()){if(command=="session")output={{"id",result["id"].ToString()},{"title",result["title"].ToString()},{"parent_id",result["parent_id"].ToString()}};else output={{"id",result["id"].ToString()},{"session_id",result["session_id"].ToString()},{"state",result["state"].ToString()},{"parent_id",result["parent_id"].ToString()},{"node_id",result["node_id"].ToString()}};}
        }
        std::cout<<output.dump()<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
}
