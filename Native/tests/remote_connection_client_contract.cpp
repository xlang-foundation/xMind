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
    httplib::Server server;std::thread owner;std::atomic<unsigned> requests=0,redirects=0,reads=0;std::atomic<int> mode=0,read_mode=0;std::atomic_bool valid=true;std::string origin;
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
    std::cout<<"Remote scoped session observation passed two actual controlled GETs and16 exact refusal GETs, fixed binding headers, strict identity/schema/size checks, preflight scope/action/expiry/path/cancellation checks and no redirect/retry. No TLS trust or remote mutation claim.\n";
    std::cout<<"Native remote client made "<<peer.requests<<" controlled bearer-authenticated loopback negotiation requests: positive grant, expired/mismatched/incompatible/malformed responses, HTTP401/403/307 and bounded-response refusal; no retry, downgrade or redirect. Unsafe/default HTTP origins and pre-cancelled calls sent nothing. No TLS trust, profile activation or persistence claim.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
