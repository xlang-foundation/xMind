#include "agentflow/remote_connection_client.hpp"
#include "agentflow/http_stream_transport.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <iostream>
#include <thread>
using namespace agentflow;
using Json=nlohmann::json;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SecretBytes secret(const std::string& text){return SecretBytes({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});}
struct Peer {
    std::atomic<unsigned> run_reads=0;std::atomic<int> run_mode=0;
    httplib::Server server;std::thread owner;std::atomic<unsigned> requests=0,redirects=0,reads=0,creates=0,receipts=0;std::atomic<int> mode=0,read_mode=0,command_mode=0;std::atomic_bool valid=true;std::string origin;
    Peer(){
        server.Post("/xmind/v1/negotiate",[&](const auto& request,auto& response){
            ++requests;try{
                if(request.get_header_value("Authorization")!="Bearer isolated-remote-credential"){response.status=401;response.set_content("private-test-secret","text/plain");return;}
                const auto body=Json::parse(request.body);if(body!=Json{{"supported_versions",Json::array({1})},{"required_capabilities",{{"runs",1},{"sessions",1},{"events",1}}},{"expected_server_id","server-a"}})valid=false;
                const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                Json result={{"server_id","server-a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",1},{"expires_at",now+60},{"authorized_scopes",Json::array({{{"id","workspace-a"},{"actions",Json::array({"sessions.read","runs.submit"})}},{{"id","workspace:a"},{"actions",Json::array({"sessions.read"})}}})},{"capabilities",{{"sessions",1},{"runs",1},{"events",1}}}};
                switch(mode.load()){
                    case 1:result["expires_at"]=now-1;break;
                    case 2:result["server_id"]="server-b";break;
                    case 3:result["protocol_version"]=2;break;
                    case 4:result["capabilities"].erase("runs");break;
                    case 5:response.set_content("{private-test-secret","application/json");return;
                    case 6:response.status=307;response.set_header("Location",origin+"/redirect-target");return;
                    case 7:response.status=403;response.set_content("private-test-secret","text/plain");return;
                    case 8:response.set_content(std::string(65537,'x'),"application/json");return;
                }
                response.set_content(result.dump(),"application/json");
            }catch(...){valid=false;response.status=500;}
        });
        server.Post("/redirect-target",[&](const auto&,auto& response){++redirects;response.status=500;});
        server.Get("/redirect-target",[&](const auto&,auto& response){++redirects;response.status=500;});
        server.Get("/xmind/v1/scopes/workspace:a/sessions/session:a",[&](const auto& request,auto& response){++reads;if(request.target!="/xmind/v1/scopes/workspace%3Aa/sessions/session%3Aa"||request.get_header_value("Authorization")!="Bearer isolated-remote-credential"||request.get_header_value("X-XMind-Scope-ID")!="workspace:a"||request.get_header_value("X-XMind-Server-ID")!="server-a"||request.get_header_value("X-XMind-Grant-ID")!="grant-a"||request.get_header_value("X-XMind-Grant-Revision")!="1")valid=false;response.set_content(Json{{"binding",{{"server_id","server-a"},{"scope_id","workspace:a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",1}}},{"session",{{"id","session:a"},{"title","Encoded scoped title"},{"revision",4}}}}.dump(),"application/json");});
        server.Get("/xmind/v1/scopes/workspace-a/sessions/session-a",[&](const auto& request,auto& response){
            ++reads;if(request.get_header_value("Authorization")!="Bearer isolated-remote-credential"){response.status=401;response.set_content("private-test-secret","text/plain");return;}
            if(!request.body.empty()||request.get_header_value("X-XMind-Protocol-Version")!="1"||request.get_header_value("X-XMind-Server-ID")!="server-a"||request.get_header_value("X-XMind-Scope-ID")!="workspace-a"||request.get_header_value("X-XMind-Grant-ID")!="grant-a"||request.get_header_value("X-XMind-Grant-Revision")!="1")valid=false;
            Json result={{"binding",{{"server_id","server-a"},{"scope_id","workspace-a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",1}}},{"session",{{"id","session-a"},{"title","Observed remote title"},{"revision",3}}}};
            switch(read_mode.load()){
                case 1:result["binding"]["server_id"]="wrong-server";break;case 2:result["binding"]["scope_id"]="other-scope";break;case 3:result["binding"]["grant_id"]="other-grant";break;case 4:result["binding"]["grant_revision"]=2;break;
                case 5:result["session"]["id"]="other-session";break;case 6:result["session"]["title"]=Json::object();break;case 7:result["session"]["revision"]=0;break;
                case 8:response.set_content("{\"binding\":"+result["binding"].dump()+",\"binding\":"+result["binding"].dump()+",\"session\":"+result["session"].dump()+"}","application/json");return;
                case 9:result["private-test-secret"]="unexpected";break;case 10:response.set_content(std::string(65537,'x'),"application/json");return;
                case 11:response.status=307;response.set_header("Location",origin+"/redirect-target");return;case 12:response.status=403;response.set_content("private-test-secret","text/plain");return;
                case 13:result["binding"]["protocol_version"]=true;break;case 14:result["binding"]["grant_revision"]=true;break;case 15:response.set_content("{private-test-secret","application/json");return;case 16:result["session"]["revision"]=9007199254740992ULL;break;
            }response.set_content(result.dump(),"application/json");
        });
        server.Get("/xmind/v1/scopes/workspace-a/sessions/session:a/runs/run:a",[&](const auto& request,auto& response){
            ++run_reads;
            if(request.target!="/xmind/v1/scopes/workspace-a/sessions/session%3Aa/runs/run%3Aa"||!request.body.empty()||request.get_header_value("Authorization")!="Bearer isolated-remote-credential"||request.get_header_value("X-XMind-Protocol-Version")!="1"||request.get_header_value("X-XMind-Server-ID")!="server-a"||request.get_header_value("X-XMind-Scope-ID")!="workspace-a"||request.get_header_value("X-XMind-Grant-ID")!="grant-a"||request.get_header_value("X-XMind-Grant-Revision")!="1")valid=false;
            Json body={{"binding",{{"server_id","server-a"},{"scope_id","workspace-a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",1}}},{"run",{{"id","run:a"},{"session_id","session:a"},{"root_id","root:a"},{"state","running"},{"revision",7}}}};
            switch(run_mode.load()){
                case 1:body["binding"]["server_id"]="other-server";break;case 2:body["binding"]["scope_id"]="other-scope";break;case 3:body["binding"]["grant_id"]="other-grant";break;case 4:body["binding"]["grant_revision"]=2;break;
                case 5:body["run"]["id"]="other-run";break;case 6:body["run"]["session_id"]="other-session";break;case 7:body["run"]["root_id"]="../outside";break;case 8:body["run"]["state"]="unknown";break;case 9:body["run"]["revision"]=0;break;case 10:body["run"]["revision"]=9007199254740992ULL;break;
                case 11:body["run"]["private-test-secret"]="unexpected";break;case 12:body["binding"]["protocol_version"]=true;break;case 13:body["run"]["revision"]=true;break;
                case 14:response.status=307;response.set_header("Location",origin+"/redirect-target");return;case 15:response.status=403;response.set_content("private-test-secret","text/plain");return;case 16:response.set_content(std::string(65537,'x'),"application/json");return;
                case 17:response.set_content("{\"binding\":"+body["binding"].dump()+",\"run\":"+body["run"].dump()+",\"run\":"+body["run"].dump()+"}","application/json");return;
                case 18:response.set_content("{private-test-secret","application/json");return;
                case 19:body["run"]["state"]="queued";break;case 20:body["run"]["state"]="paused";break;case 21:body["run"]["state"]="completed";break;case 22:body["run"]["state"]="failed";break;case 23:body["run"]["state"]="cancelled";break;
            }response.set_content(body.dump(),"application/json");
        });
        const auto reply=[&](const auto& request,auto& response,bool create){
          if(create)++creates;else ++receipts;
          if(request.get_header_value("Authorization")!="Bearer isolated-remote-credential"||request.get_header_value("X-XMind-Server-ID")!="server-a"||request.get_header_value("X-XMind-Scope-ID")!="workspace-a"||request.get_header_value("X-XMind-Grant-ID")!="grant-a"||request.get_header_value("X-XMind-Grant-Revision")!="1"||request.get_header_value("X-XMind-Protocol-Version")!="1")valid=false;
          if(create&&Json::parse(request.body)!=Json{{"request_id","request:a"},{"operation","sessions.create"},{"expected_revision",0},{"title","Exact title"}})valid=false;if(!create&&(!request.body.empty()||request.target!="/xmind/v1/scopes/workspace-a/requests/request%3Aa"))valid=false;
          Json body={{"binding",{{"server_id","server-a"},{"scope_id","workspace-a"},{"protocol_version",1},{"grant_id","grant-a"},{"grant_revision",1}}},{"receipt",{{"request_id","request:a"},{"operation","sessions.create"},{"expected_revision",0},{"title","Exact title"},{"state","admitted"},{"resource_id","created-session"},{"resource_revision",1}}}};
          switch(command_mode.load()){
            case 1:body["receipt"]["title"]="Different intent";break;case 2:body["receipt"]["request_id"]="wrong-request";break;case 3:body["binding"]["scope_id"]="other-scope";break;case 4:body["receipt"]["resource_revision"]=9007199254740992ULL;break;case 5:body["receipt"]["expected_revision"]=true;break;
            case 6:body["receipt"]={{"request_id","request:a"},{"state","not_found"}};break;
            case 13:body["receipt"]={{"request_id","wrong-request"},{"state","not_found"}};break;case 14:body["receipt"]={{"request_id","request:a"},{"state","not_found"},{"title","Unexpected field"}};break;case 15:body["receipt"]={{"state","not_found"}};break;
            case 7:response.status=307;response.set_header("Location",origin+"/redirect-target");return;case 8:response.status=403;response.set_content("private-test-secret","text/plain");return;case 9:response.set_content(std::string(65537,'x'),"application/json");return;
            case 10:body["receipt"]["state"]="pending";body["receipt"]["resource_id"]="";body["receipt"]["resource_revision"]=0;break;case 11:body["receipt"]["state"]="rejected";body["receipt"]["resource_id"]="";body["receipt"]["resource_revision"]=0;break;
            case 12:response.set_content("{\"binding\":"+body["binding"].dump()+",\"receipt\":"+body["receipt"].dump()+",\"receipt\":"+body["receipt"].dump()+"}","application/json");return;
          }response.set_content(body.dump(),"application/json");
        };
        server.Post("/xmind/v1/scopes/workspace-a/sessions",[reply](const auto& request,auto& response){reply(request,response,true);});server.Get("/xmind/v1/scopes/workspace-a/requests/request:a",[reply](const auto& request,auto& response){reply(request,response,false);});
        const auto port=server.bind_to_any_port("127.0.0.1");require(port>0,"Remote test peer bind failed");origin="http://127.0.0.1:"+std::to_string(port);owner=std::thread([&]{server.listen_after_bind();});
    }
    ~Peer(){server.stop();if(owner.joinable())owner.join();}
};
}
int main(){try{
    Peer peer;auto credential=secret("isolated-remote-credential");RemoteNegotiationRequirements requirements;requirements.expected_server_id="server-a";requirements.required_capabilities={{"sessions",1},{"runs",1},{"events",1}};
    const auto invoke=[&]{return negotiate_remote_connection(peer.origin,credential,requirements,2s,RemoteTransportPolicy::isolated_loopback_test);};
    const auto grant=invoke();require(grant.server_id=="server-a"&&grant.grant_id=="grant-a"&&grant.authorized_scopes.front().id=="workspace-a","Native negotiation must retain the verified response fields");
    const auto rejected=[&](int mode,RemoteNegotiationFailure reason){peer.mode=mode;const auto before=peer.requests.load();bool refused=false;try{invoke();}catch(const RemoteNegotiationError& e){refused=e.reason()==reason&&std::string(e.what()).find("private-test-secret")==std::string::npos;}require(refused&&peer.requests==before+1,"Refused metadata must issue exactly one request without downgrade/retry");};
    rejected(1,RemoteNegotiationFailure::expired_grant);rejected(2,RemoteNegotiationFailure::server_mismatch);rejected(3,RemoteNegotiationFailure::unsupported_version);rejected(4,RemoteNegotiationFailure::missing_capability);rejected(5,RemoteNegotiationFailure::malformed);
    for(const auto mode:{6,7}){peer.mode=mode;const auto before=peer.requests.load();bool refused=false;try{invoke();}catch(const ProviderHttpError& e){refused=e.status==(mode==6?307:403)&&std::string(e.what()).find("private-test-secret")==std::string::npos;}require(refused&&peer.requests==before+1&&peer.redirects==0,"HTTP refusal cannot redirect credentials or retry negotiation");}
    peer.mode=8;{bool refused=false;const auto before=peer.requests.load();try{invoke();}catch(const TransportError&){refused=true;}require(refused&&peer.requests==before+1,"Oversized negotiation response must refuse once");}
    peer.mode=0;{auto wrong=secret("wrong-isolated-credential");const auto before=peer.requests.load();bool refused=false;try{negotiate_remote_connection(peer.origin,wrong,requirements,2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const ProviderHttpError& e){refused=e.status==401;}require(refused&&peer.requests==before+1,"Wrong credential must receive one authoritative refusal");}
    const auto before=peer.requests.load();
    for(const auto& origin:{peer.origin,peer.origin+"/path",std::string("http://remote.invalid"),std::string("https://:443"),std::string("https://user:private-test-secret@example.invalid"),std::string("https://example.invalid?token=private-test-secret")}){bool refused=false;try{negotiate_remote_connection(origin,credential,requirements,2s);}catch(const std::invalid_argument&){refused=true;}require(refused,"Default policy must reject unsafe origins before send");}
    for(const auto& origin:{peer.origin+"/path",peer.origin+"?q=x",std::string("http://127.0.0.1:0"),std::string("http://localhost:1234"),std::string("http://127.0.0.1:65536")}){bool refused=false;try{negotiate_remote_connection(origin,credential,requirements,2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const std::invalid_argument&){refused=true;}require(refused,"Test transport exception must remain exact loopback origin only");}
    {std::stop_source stopped;stopped.request_stop();bool refused=false;try{negotiate_remote_connection(peer.origin,credential,requirements,2s,RemoteTransportPolicy::isolated_loopback_test,stopped.get_token());}catch(const TransportCancelled&){refused=true;}require(refused,"Pre-cancelled negotiation must not send");}
    require(peer.requests==before&&peer.valid&&peer.redirects==0,"Invalid local inputs and cancellation cannot send requests or leak redirects");
    const auto observe=[&]{return read_remote_session(peer.origin,credential,grant,"workspace-a","session-a",2s,RemoteTransportPolicy::isolated_loopback_test);};
    const auto observed=observe();require(observed.id=="session-a"&&observed.title=="Observed remote title"&&observed.revision==3,"Remote session read must retain exact typed metadata");
    const auto encoded=read_remote_session(peer.origin,credential,grant,"workspace:a","session:a",2s,RemoteTransportPolicy::isolated_loopback_test);require(encoded.id=="session:a"&&encoded.title=="Encoded scoped title"&&encoded.revision==4,"Opaque scope/resource colons must be encoded exactly once and retain original identity");
    for(int mode=1;mode<=16;++mode){peer.read_mode=mode;const auto sent=peer.reads.load();bool refused=false;try{observe();}catch(const TransportError& error){refused=std::string(error.what()).find("private-test-secret")==std::string::npos;}require(refused&&peer.reads==sent+1&&peer.redirects==0,"Malformed/mismatched/oversized/error read must refuse exactly once without redirect or private peer text");}
    peer.read_mode=0;const auto reads_before=peer.reads.load();
    for(const auto& id:{std::string("."),std::string(".."),std::string("../outside"),std::string("x/y"),std::string("%2F"),std::string("?query"),std::string("#fragment")}){bool refused=false;try{read_remote_session(peer.origin,credential,grant,"workspace-a",id,2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const std::invalid_argument&){refused=true;}require(refused,"Ambiguous remote resource path must refuse before send");}
    {auto denied=grant;denied.authorized_scopes.front().actions={"runs.submit"};bool refused=false;try{read_remote_session(peer.origin,credential,denied,"workspace-a","session-a",2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const TransportError&){refused=true;}require(refused,"Grant capability alone cannot authorize a session read");}
    for(int mode=0;mode<3;++mode){auto invalid=grant;if(mode==0)invalid.expires_at=1;if(mode==1)invalid.capabilities.erase("sessions");if(mode==2)invalid.capabilities["sessions"]=2;bool refused=false;try{read_remote_session(peer.origin,credential,invalid,"workspace-a","session-a",2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const RemoteNegotiationError&){refused=true;}require(refused,"Expired or incompatible read grants must refuse before send");}
    {std::stop_source stopped;stopped.request_stop();bool refused=false;try{read_remote_session(peer.origin,credential,grant,"workspace-a","session-a",2s,RemoteTransportPolicy::isolated_loopback_test,stopped.get_token());}catch(const TransportCancelled&){refused=true;}require(refused,"Pre-cancelled remote read must not send");}
    {bool refused=false;try{read_remote_session(peer.origin,credential,grant,"workspace-a","session-a",2s);}catch(const std::invalid_argument&){refused=true;}require(refused,"Production read must reject unverified HTTP");}
    require(peer.reads==reads_before&&peer.valid&&peer.redirects==0,"Read preflight refusals must send nothing and preserve exact native binding headers");
    auto run_grant=grant;run_grant.authorized_scopes.front().actions={"runs.read"};
    const auto observe_run=[&]{return read_remote_run(peer.origin,credential,run_grant,"workspace-a","session:a","run:a",2s,RemoteTransportPolicy::isolated_loopback_test);};
    const auto observed_run=observe_run();require(observed_run.id=="run:a"&&observed_run.session_id=="session:a"&&observed_run.root_id=="root:a"&&observed_run.state=="running"&&observed_run.revision==7,"Typed run observation must preserve exact session/root identity, state and revision");
    for(int mode=1;mode<=18;++mode){peer.run_mode=mode;const auto before=peer.run_reads.load();bool refused=false;try{observe_run();}catch(const TransportError& error){refused=std::string(error.what()).find("private-test-secret")==std::string::npos;}require(refused&&peer.run_reads==before+1&&peer.redirects==0,"Run observation must refuse mismatched/malformed/error responses once without redirect or private peer text");}
    for(int mode=19;mode<=23;++mode){peer.run_mode=mode;const auto value=observe_run();require(value.state==std::vector<std::string>{"queued","paused","completed","failed","cancelled"}[mode-19],"Every supported remote run state must remain explicit");}peer.run_mode=0;
    const auto run_reads_before=peer.run_reads.load();
    for(int mode=0;mode<6;++mode){auto denied=run_grant;if(mode==0)denied.authorized_scopes.front().actions={"runs.submit"};if(mode==1)denied.authorized_scopes.front().id="other-scope";if(mode==2)denied.expires_at=1;if(mode==3)denied.capabilities.erase("runs");if(mode==4)denied.capabilities["runs"]=2;if(mode==5)denied.grant_revision=0;bool refused=false;try{read_remote_run(peer.origin,credential,denied,"workspace-a","session:a","run:a",2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const TransportError&){refused=true;}catch(const RemoteNegotiationError&){refused=true;}require(refused,"Run read requires its own compatible unexpired scoped authorization");}
    for(const std::string id:{".","..","../outside","x/y","%2F","?query","#fragment"})for(bool session:{false,true}){bool refused=false;try{read_remote_run(peer.origin,credential,run_grant,"workspace-a",session?id:"session:a",session?"run:a":id,2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const std::invalid_argument&){refused=true;}require(refused,"Run and session path components must reject ambiguous identities before send");}
    {std::stop_source stopped;stopped.request_stop();bool refused=false;try{read_remote_run(peer.origin,credential,run_grant,"workspace-a","session:a","run:a",2s,RemoteTransportPolicy::isolated_loopback_test,stopped.get_token());}catch(const TransportCancelled&){refused=true;}require(refused,"Pre-cancelled run observation cannot send");}
    {bool refused=false;try{read_remote_run(peer.origin,credential,run_grant,"workspace-a","session:a","run:a",2s);}catch(const std::invalid_argument&){refused=true;}require(refused,"Production run observation cannot permit unverified HTTP");}
    require(peer.run_reads==run_reads_before&&peer.valid&&peer.redirects==0,"All run preflight refusals must preserve no-send and exact binding headers");
    auto command_grant=grant;command_grant.capabilities["receipts"]=1;command_grant.authorized_scopes.front().actions={"sessions.read","sessions.create","receipts.read"};
    const auto create=[&]{return create_remote_session(peer.origin,credential,command_grant,"workspace-a","request:a","Exact title",2s,RemoteTransportPolicy::isolated_loopback_test);};const auto receipt=[&]{return observe_remote_session_request(peer.origin,credential,command_grant,"workspace-a","request:a","Exact title",2s,RemoteTransportPolicy::isolated_loopback_test);};
    require(create().resolution->resource_id=="created-session"&&receipt().resolution->resource_id=="created-session","Typed command and receipt paths must confirm exact request intent");
    for(const auto mode:{1,2,3,4,5,7,8,9,12,13,14,15}){peer.command_mode=mode;for(bool post:{true,false}){const auto before=post?peer.creates.load():peer.receipts.load();bool refused=false;try{if(post)create();else receipt();}catch(const TransportError& e){refused=std::string(e.what()).find("private-test-secret")==std::string::npos;}require(refused&&(post?peer.creates.load():peer.receipts.load())==before+1&&peer.redirects==0,"Invalid command response must refuse once without retry/redirect");}}
    peer.command_mode=6;require(receipt().state=="not_found"&&!receipt().resolution,"Receipt absence cannot invent a terminal result");{bool refused=false;try{create();}catch(const TransportError&){refused=true;}require(refused,"Creation cannot return not-found as an admission");}for(const auto mode:{10,11}){peer.command_mode=mode;require(create().state==(mode==10?"pending":"rejected"),"Command state must remain authoritative");}
    peer.command_mode=0;const auto creates_before=peer.creates.load();auto denied=command_grant;denied.authorized_scopes.front().actions={"sessions.read","receipts.read"};{bool refused=false;try{create_remote_session(peer.origin,credential,denied,"workspace-a","request:a","Exact title",2s,RemoteTransportPolicy::isolated_loopback_test);}catch(const TransportError&){refused=true;}require(refused&&peer.creates==creates_before,"Missing creation action cannot send");}require(peer.valid&&peer.redirects==0,"Exact typed binding and command body required");
    std::cout<<"Actual native typed session POST and receipt GET pass exact request/title/revision binding, admission/pending/rejection/not-found distinctions, malformed/error/redirect/size refusal and no retry. Loopback test only; no production activation.\n";
    std::cout<<"Remote scoped session observation passed two actual controlled GETs and16 exact refusal GETs, fixed binding headers, strict identity/schema/size checks, preflight scope/action/expiry/path/cancellation checks and no redirect/retry. No TLS trust or remote mutation claim.\n";
    std::cout<<"Native remote client made "<<peer.requests<<" controlled bearer-authenticated loopback negotiation requests: positive grant, expired/mismatched/incompatible/malformed responses, HTTP401/403/307 and bounded-response refusal; no retry, downgrade or redirect. Unsafe/default HTTP origins and pre-cancelled calls sent nothing. No TLS trust, profile activation or persistence claim.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
