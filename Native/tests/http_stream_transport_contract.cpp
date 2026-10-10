#include "agentflow/http_stream_transport.hpp"
#include "agentflow/model_stream.hpp"
#include "agentflow/mcp_http_transport.hpp"
#include "agentflow/mcp_oauth.hpp"
#include "agentflow/mcp_http_client.hpp"
#include <iostream>
#include <thread>
#include <utility>

using namespace agentflow;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected transport rejection did not occur");
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        const std::string base=argv[1],tls=argv[2];
        const std::string synthetic="transport-test-token-not-a-real-key";
        SecretBytes secret({reinterpret_cast<const std::uint8_t*>(synthetic.data()),synthetic.size()});
        auto request=[&](const std::string& path) {return HttpStreamRequest{base+path,R"({"fixture":"transport"})",10s,5s};};
        auto discovery=[&](const std::string& path){return HttpStreamRequest{base+path,"",10s,5s};};
        const std::string json_post_body=R"({"input":"native )"+std::string("\xf0\x9f\x8c\x8d")+R"(","decimal":1.00000000000000000001,"pa\u0074h":"raw"})";
        const std::string json_post_reply=R"({"input_tokens":19,"opaque":"synthetic )"+std::string("\xf0\x9f\x8c\x8d")+R"("})";
        auto post_request=[&](const std::string& path){return HttpStreamRequest{base+"/post-json/"+path,json_post_body,10s,5s};};
        for(const auto mode:{CredentialHeader::x_api_key,CredentialHeader::x_goog_api_key}){
            const std::string name=mode==CredentialHeader::x_api_key?"api-key":"google-key";
            auto input=request("/auth/"+name);input.credential_header=mode;
            ChatCompletionStream decoder([](const ModelEvent&){});post_event_stream(input,&secret,[&](std::string_view bytes){decoder.feed(bytes);});require(decoder.finish().content=="transport fixture","API-key header must retain actual streaming bytes");
            auto json=discovery("/json-auth/"+name);json.credential_header=mode;require(get_json(json,&secret)==R"({"object":"list","data":[]})","API-key JSON discovery must use the selected header");
            input.url+="/redirect";rejects<ProviderHttpError>([&]{post_event_stream(input,&secret,[](std::string_view){});});
            rejects<std::invalid_argument>([&]{post_event_stream(input,nullptr,[](std::string_view){});});
            const std::string injected="transport-fixture\r\nx-extra: injected";SecretBytes invalid({reinterpret_cast<const std::uint8_t*>(injected.data()),injected.size()});
            rejects<std::invalid_argument>([&]{post_event_stream(input,&invalid,[](std::string_view){});});
        }
        {auto invalid=request("/ok");invalid.credential_header=static_cast<CredentialHeader>(999);rejects<std::invalid_argument>([&]{post_event_stream(invalid,&secret,[](std::string_view){});});}
        {auto invalid=request("/ok");invalid.protocol=static_cast<ProviderHttpProtocol>(999);rejects<std::invalid_argument>([&]{post_event_stream(invalid,&secret,[](std::string_view){});});}
        require(get_json(discovery("/json"),&secret)==R"({"object":"list","data":[]})","JSON GET must return actual native peer bytes");
        rejects<ProviderHttpError>([&]{get_json(discovery("/json-redirect"),&secret);});
        rejects<TransportError>([&]{get_json(discovery("/json-wrong-media"),&secret);});
        rejects<TransportError>([&]{get_json(discovery("/json-oversized"),&secret);});
        rejects<TransportError>([&]{get_json({tls+"/json","",10s,5s},nullptr);});
        rejects<std::invalid_argument>([&]{get_json({"http://example.invalid/models",""},&secret);});
        {
            std::stop_source cancellation;
            std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
            rejects<TransportCancelled>([&]{get_json(discovery("/json-delay"),&secret,cancellation.get_token());});
        }
        {auto timed=discovery("/json-delay");timed.deadline=200ms;rejects<TransportTimeout>([&]{get_json(timed,&secret);});}
        // Native non-streaming JSON POST is shared infrastructure for context
        // APIs. These are real socket/header/byte checks against a synthetic
        // peer, not token-counting or model-compaction acceptance.
        for(const auto mode:{CredentialHeader::bearer,CredentialHeader::x_api_key,CredentialHeader::x_goog_api_key}){
            const std::string name=mode==CredentialHeader::bearer?"bearer":mode==CredentialHeader::x_api_key?"api-key":"google-key";
            auto input=post_request(name);input.credential_header=mode;
            require(post_json(input,&secret,json_post_reply.size())==json_post_reply,"JSON POST must preserve exact UTF-8 response bytes and selected credentials");
            if(mode!=CredentialHeader::bearer)rejects<std::invalid_argument>([&]{post_json(input,nullptr,1024);});
        }
        {auto input=post_request("claude-protocol");input.credential_header=CredentialHeader::x_api_key;input.protocol=ProviderHttpProtocol::anthropic;
         require(post_json(input,&secret,1024)==json_post_reply,"JSON POST must use the selected Claude protocol header");}
        require(post_json(post_request("boundary"),&secret,json_post_reply.size())==json_post_reply,"JSON POST must accept exactly its byte limit");
        rejects<TransportError>([&]{post_json(post_request("boundary"),&secret,json_post_reply.size()-1);});
        const std::string large_reply=R"({"opaque":")"+std::string(1024*1024+257,'x')+R"("})";
        require(post_json(post_request("large"),&secret,2*1024*1024)==large_reply,"JSON POST must honor its explicit limit independently of discovery's 1 MiB cap");
        rejects<TransportError>([&]{post_json(post_request("oversized"),&secret,64);});
        rejects<TransportError>([&]{post_json(post_request("wrong-media"),&secret,1024);});
        rejects<ProviderHttpError>([&]{post_json(post_request("redirect"),&secret,1024);});
        {bool rejected=false;try{post_json(post_request("diagnostic"),&secret,1024);}catch(const ProviderHttpError& error){
            rejected=true;require(error.status==400&&error.type=="invalid_request_error"&&error.code=="context_length_exceeded"&&error.param=="input","JSON POST must preserve allowlisted context-limit diagnostics");
            require(std::string(error.what()).find("private-json-post")==std::string::npos,"JSON POST diagnostics must not expose response messages");
         }require(rejected,"JSON POST diagnostic must remain an HTTP failure");}
        {std::stop_source cancellation;std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
         rejects<TransportCancelled>([&]{post_json(post_request("delay"),&secret,1024,cancellation.get_token());});}
        {auto timed=post_request("delay");timed.deadline=200ms;rejects<TransportTimeout>([&]{post_json(timed,&secret,1024);});}
        {auto timed=post_request("stall");timed.idle_timeout=200ms;rejects<TransportTimeout>([&]{post_json(timed,&secret,1024);});}
        {auto input=post_request("pre-cancel");std::stop_source cancellation;cancellation.request_stop();
         rejects<TransportCancelled>([&]{post_json(input,&secret,1024,cancellation.get_token());});}
        {auto input=post_request("invalid");rejects<std::invalid_argument>([&]{post_json(input,&secret,0);});rejects<std::invalid_argument>([&]{post_json(input,&secret,8*1024*1024+1);});
         input.body.clear();rejects<std::invalid_argument>([&]{post_json(input,&secret,1024);});
         input.body=std::string(8*1024*1024+1,'x');rejects<std::invalid_argument>([&]{post_json(input,&secret,1024);});
         input.body=json_post_body;const std::string injected="transport-fixture\r\nx-extra: injected";SecretBytes invalid({reinterpret_cast<const std::uint8_t*>(injected.data()),injected.size()});rejects<std::invalid_argument>([&]{post_json(input,&invalid,1024);});}
        rejects<TransportError>([&]{post_json({tls+"/post-json/bearer",json_post_body,10s,5s},&secret,1024);});
        rejects<std::invalid_argument>([&]{post_json({"http://example.invalid/",json_post_body},&secret,1024);});
        rejects<std::invalid_argument>([&]{post_json({"https://user:password@example.invalid/",json_post_body},&secret,1024);});
        require(post_json(post_request("after-failure"),&secret,1024)==json_post_reply,"Fresh JSON POST must work after cancellation, TLS and boundary failures");
        {
            ChatCompletionStream decoder([](const ModelEvent&){});
            post_event_stream(request("/ok"),&secret,[&](std::string_view bytes){decoder.feed(bytes);});
            require(decoder.finish().content=="transport fixture","Native socket bytes must reach decoder");
        }
        {
            bool error=false;
            try {post_event_stream(request("/error"),&secret,[](std::string_view){});}
            catch(const ProviderHttpError& exception) {
                require(exception.status==429,"Provider HTTP status must survive");
                require(std::string(exception.what()).find("do-not-log")==std::string::npos,"Provider error body must not be echoed");error=true;
            }
            require(error,"HTTP error must fail transport");
        }
        rejects<ProviderHttpError>([&]{post_event_stream(request("/redirect"),&secret,[](std::string_view){});});
        for(const auto* path:{"/diagnostic","/diagnostic-private","/diagnostic-large","/diagnostic-malformed","/diagnostic-stall"}) {
            bool rejected=false;auto input=request(path);input.deadline=500ms;
            try {post_event_stream(input,&secret,[](std::string_view){throw std::runtime_error("Error body reached consumer");});}
            catch(const ProviderHttpError& error) {
                rejected=true;require(error.status==400,"Diagnostic failures must preserve HTTP status");
                const auto detail=std::string(error.what())+error.type+error.code+error.param;
                require(detail.find("private-fixture-key")==std::string::npos && detail.find("do-not-log")==std::string::npos,"Diagnostics cannot expose private or message fields");
                if(std::string(path)=="/diagnostic" || std::string(path)=="/diagnostic-private") {
                    require(error.type=="invalid_request_error" && error.code=="unsupported_parameter","Known diagnostic identifiers must survive");
                    require(error.param==(std::string(path)=="/diagnostic"?"n":""),"Only allowlisted parameters may survive");
                } else require(error.type.empty()&&error.code.empty()&&error.param.empty(),"Incomplete diagnostics must be omitted");
            }
            require(rejected,"HTTP failure must remain an error");
        }
        {
            std::stop_source cancellation;
            std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
            rejects<TransportCancelled>([&]{post_event_stream(request("/diagnostic-stall"),&secret,[](std::string_view){},cancellation.get_token());});
        }
        rejects<TransportError>([&]{post_event_stream(request("/wrong-media"),&secret,[](std::string_view){});});
        rejects<TransportError>([&]{post_event_stream({tls+"/ok","{}",10s,5s},nullptr,[](std::string_view){});});
        rejects<std::invalid_argument>([&]{post_event_stream({"http://example.invalid/","{}"},&secret,[](std::string_view){});});
        rejects<std::invalid_argument>([&]{post_event_stream({"https://user:password@example.invalid/","{}"},&secret,[](std::string_view){});});
        {
            std::stop_source cancellation;cancellation.request_stop();
            rejects<TransportCancelled>([&]{post_event_stream(request("/ok"),&secret,[](std::string_view){},cancellation.get_token());});
        }
        {
            std::stop_source cancellation;
            std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});
            const auto started=std::chrono::steady_clock::now();
            rejects<TransportCancelled>([&]{post_event_stream(request("/delay"),&secret,[](std::string_view){},cancellation.get_token());});
            require(std::chrono::steady_clock::now()-started<2s,"Cancellation must interrupt pending response");
        }
        {
            auto timed=request("/stall");timed.deadline=200ms;
            rejects<TransportTimeout>([&]{post_event_stream(timed,&secret,[](std::string_view){});});
        }
        {
            auto timed=request("/stall");timed.idle_timeout=200ms;
            rejects<TransportTimeout>([&]{post_event_stream(timed,&secret,[](std::string_view){});});
        }
        {
            bool stopped=false;
            try {post_event_stream(request("/ok"),&secret,[](std::string_view){throw std::logic_error("Consumer stopped");});}
            catch(const std::logic_error&) {stopped=true;}require(stopped,"Consumer exception must propagate");
        }
        // Fresh request after cancellation/exception checks cleanup and isolation.
        {ChatCompletionStream decoder([](const ModelEvent&){});post_event_stream(request("/ok"),&secret,[&](std::string_view bytes){decoder.feed(bytes);});require(decoder.finish().finish_reason=="stop","Transport must remain usable");}
        const auto mcpBody=mcp_request("http-fixture","tools/call",R"({"name":"雪","arguments":{"n":9007199254740991,"region":"line1\nline2","precise":1.00000000000000000001}})",McpWireEra::modern);
        const auto mcpSchema=R"({"properties":{"n":{"type":"integer","x-mcp-header":"Count"},"region":{"type":"string","x-mcp-header":"Region"}}})";
        auto mcpInput=[&](const std::string& path){McpHttpPost input;input.url=base+"/mcp/"+path;input.body=mcpBody;input.input_schema=mcpSchema;input.deadline=10s;input.idle_timeout=5s;return input;};
        const std::string mcpReply=R"({"jsonrpc":"2.0","id":"http-fixture","result":{"resultType":"complete","content":[{"type":"text","text":"actual 雪 bytes"}]}})";
        auto exchange=[&](const McpHttpPost& input,std::stop_token cancel=std::stop_token{}) {
            McpHttpResponseHead head;std::string body;int sent=0,heads=0;
            post_mcp_http(input,&secret,[&](const auto& h){require(sent==1 && heads++==0,"MCP headers must follow one conservative send boundary");head=h;},[&](std::string_view bytes){require(heads==1,"MCP bytes must follow response metadata");body+=bytes;},[&]{++sent;},cancel);
            require(sent==1 && heads==1,"MCP POST must not retry or omit response metadata");return std::pair{head,body};
        };
        {const auto [head,body]=exchange(mcpInput("json"));require(head.status==200 && head.media_type=="application/json" && body==mcpReply,"Native MCP JSON response must preserve exact UTF-8 bytes");}
        {const auto [head,body]=exchange(mcpInput("sse"));require(head.status==200 && head.media_type=="text/event-stream" && body=="event: message\ndata: "+mcpReply+"\n\n","Native MCP transport must accept actual request-scoped SSE bytes");}
        {const auto [head,body]=exchange(mcpInput("error"));require(head.status==400 && body==R"({"jsonrpc":"2.0","id":"http-fixture","error":{"code":-32020,"message":"fixture mismatch"}})","MCP JSON-RPC HTTP errors must remain attributable, not provider errors");}
        {const auto [head,body]=exchange(mcpInput("auth"));require(head.status==401 && head.authenticate=="Bearer resource_metadata=\"http://127.0.0.1/resource\"" && body.empty(),"MCP authorization challenge must remain backend-private protocol metadata");}
        {auto input=mcpInput("legacy");input.era=McpWireEra::legacy;input.body=mcp_request("http-fixture","tools/list","{}",McpWireEra::legacy);input.input_schema.reset();input.legacy_session="fixture-session";
         const auto [head,body]=exchange(input);require(head.status==200 && head.legacy_session=="fixture-next-session" && body==mcpReply,"Older session request/response metadata must survive native transport");}
        auto notice=mcpInput("notification");notice.body="{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":"+*mcp_object_member(mcp_request("notice","notifications/progress","{}",McpWireEra::modern),"params")+"}";notice.input_schema.reset();
        {const auto [head,body]=exchange(notice);require(head.status==202 && body.empty(),"Empty notification acknowledgement must succeed without Content-Type");}
        notice.url=base+"/mcp/bad-ack";rejects<TransportError>([&]{exchange(notice);});
        {const auto [head,body]=exchange(mcpInput("redirect"));require(head.status==302 && body.empty(),"MCP redirect must surface its status without forwarding credentials");}
        rejects<TransportError>([&]{exchange(mcpInput("wrong-media"));});
        {auto input=mcpInput("delay");std::stop_source cancellation;std::jthread canceller([&]{std::this_thread::sleep_for(150ms);cancellation.request_stop();});rejects<TransportCancelled>([&]{exchange(input,cancellation.get_token());});}
        {auto input=mcpInput("delay");input.deadline=200ms;rejects<TransportTimeout>([&]{exchange(input);});}
        {auto input=mcpInput("invalid");int sent=0;auto call=[&](std::stop_token cancel={}){post_mcp_http(input,&secret,[](const auto&){},[](std::string_view){},[&]{++sent;},cancel);};
         std::stop_source cancellation;cancellation.request_stop();rejects<TransportCancelled>([&]{call(cancellation.get_token());});
         input.legacy_session="injected\r\nX-Extra: invalid";rejects<McpProtocolError>([&]{call();});input.legacy_session.reset();
         input.input_schema=R"({"properties":{"n":{"type":"integer","x-mcp-header":"Bad\r\nName"}}})";rejects<McpProtocolError>([&]{call();});input.input_schema=mcpSchema;
         input.url="http://example.invalid/mcp";rejects<std::invalid_argument>([&]{call();});
         require(sent==0,"Invalid metadata, pre-cancellation and remote plaintext must stop before the send boundary");
         input.url=tls+"/mcp/json";rejects<TransportError>([&]{call();});require(sent<=1,"Invalid configuration must not dispatch; untrusted TLS may cross the conservative send boundary");}
        {const auto [head,body]=exchange(mcpInput("after-failure"));require(head.status==200 && body==mcpReply,"Fresh native MCP POST must survive prior cancellation and TLS failures");}
        const auto resourceUrls=mcp_oauth_resource_metadata_urls("https://resource.example.test/public/mcp");require(resourceUrls==std::vector<std::string>{"https://resource.example.test/.well-known/oauth-protected-resource/public/mcp","https://resource.example.test/.well-known/oauth-protected-resource"},"Resource metadata must use path insertion before root fallback");
        const auto issuerUrls=mcp_oauth_authorization_metadata_urls("https://issuer.example.test/tenant");require(issuerUrls==std::vector<std::string>{"https://issuer.example.test/.well-known/oauth-authorization-server/tenant","https://issuer.example.test/.well-known/openid-configuration/tenant","https://issuer.example.test/tenant/.well-known/openid-configuration"},"OAuth and both OpenID path forms must preserve discovery priority");
        require(mcp_oauth_authorization_metadata_urls("https://issuer.example.test").size()==2,"Root issuer must have two distinct discovery candidates");
        const auto resource=mcp_oauth_resource_metadata(R"({"resource":"https://resource.example.test/mcp","authorization_servers":["https://issuer.example.test/tenant"],"scopes_supported":["files:read"],"bearer_methods_supported":["header"]})","https://resource.example.test/mcp");require(resource.authorization_servers.size()==1 && resource.scopes==std::vector<std::string>{"files:read"},"Resource metadata must retain distinct issuer and scope identities");
        const std::string authorization=R"({"issuer":"https://issuer.example.test/tenant","authorization_endpoint":"https://issuer.example.test/authorize","token_endpoint":"https://issuer.example.test/token","response_types_supported":["code"],"code_challenge_methods_supported":["S256"],"token_endpoint_auth_methods_supported":["none"],"authorization_response_iss_parameter_supported":true,"client_id_metadata_document_supported":true})";
        const auto metadata=mcp_oauth_server_metadata(authorization,"https://issuer.example.test/tenant");require(metadata.response_issuer_required && metadata.client_id_metadata_supported && metadata.token_auth_methods==std::vector<std::string>{"none"},"Validated metadata must preserve issuer validation and registration capabilities");
        rejects<McpProtocolError>([&]{mcp_oauth_server_metadata(authorization,"https://ISSUER.example.test/tenant");});
        rejects<McpProtocolError>([&]{mcp_oauth_server_metadata(authorization,"https://issuer.example.test/tenant/");});
        rejects<McpProtocolError>([&]{mcp_oauth_resource_metadata(R"({"resource":"https://other.example.test/mcp","authorization_servers":["https://issuer.example.test"]})","https://resource.example.test/mcp");});
        rejects<McpProtocolError>([&]{mcp_oauth_resource_metadata(R"({"resource":"https://resource.example.test/mcp","authorization_servers":[]})","https://resource.example.test/mcp");});
        rejects<McpProtocolError>([&]{mcp_oauth_server_metadata(R"({"issuer":"https://issuer.example.test","authorization_endpoint":"https://issuer.example.test/auth","token_endpoint":"https://issuer.example.test/token","response_types_supported":["code"],"code_challenge_methods_supported":["plain"]})","https://issuer.example.test");});
        const auto challenge=mcp_oauth_bearer_challenge(R"(Basic realm="not, bearer", charset="UTF-8", bEaReR resource_metadata="https://resource.example.test/.well-known/oauth-protected-resource", scope="files:read files:write", error="insufficient_scope")");
        require(challenge && challenge->metadata_url && challenge->scopes==std::vector<std::string>{"files:read","files:write"} && challenge->error=="insufficient_scope","Bearer challenge must distinguish quoted commas and alternative authentication schemes");
        require(!mcp_oauth_bearer_challenge("Negotiate opaque-token=="),"Non-Bearer token challenges must not create OAuth authority");
        for(const auto* bad:{"Bearer scope=\"files:read\", Scope=\"files:write\"","Bearer scope=\"files:read\", Bearer scope=\"files:write\"","Bearer scope=\"unterminated","Bearer scope=\"invalid\\\\scope\"","Bearer scope=\"read\"\r\nInjected: bad"})rejects<McpProtocolError>([&]{mcp_oauth_bearer_challenge(bad);});
        rejects<McpProtocolError>([&]{mcp_oauth_authorization_metadata_urls("http://127.0.0.1/issuer");});
        rejects<McpProtocolError>([&]{mcp_oauth_authorization_metadata_urls("https://issuer.example.test/tenant?redirect=other");});
        require(mcp_oauth_bearer_challenge(R"(Bearer resource_metadata="https://resource.example.test/metadata?tenant=one")")->metadata_url=="https://resource.example.test/metadata?tenant=one","Protected resource metadata URL query must retain its exact identity");
        {
            SecretBytes credential({reinterpret_cast<const std::uint8_t*>(synthetic.data()),synthetic.size()});
            McpHttpClient owner(base+"/oauth-auth",std::move(credential));bool required=false;
            try{owner.connect(std::chrono::steady_clock::now()+5s);}
            catch(const McpOAuthAuthorizationRequired& error){required=true;require(error.challenge && error.challenge->metadata_url=="https://resource.example.test/metadata?tenant=one" && error.challenge->scopes==std::vector<std::string>{"files:read"},"Native owner must retain Bearer challenge after a separate Basic field");}
            require(required && !owner.ready(),"401 must retire the owner without consuming or displaying private error body");
            rejects<McpProtocolError>([&]{owner.connect(std::chrono::steady_clock::now()+5s);});
        }
        {std::stop_source cancel;cancel.request_stop();rejects<McpTransportCancelled>([&]{discover_mcp_oauth("https://resource.example.test/mcp",{},std::chrono::steady_clock::now()+5s,cancel.get_token());});}
        rejects<McpTransportTimeout>([&]{discover_mcp_oauth("https://resource.example.test/mcp",{},std::chrono::steady_clock::now());});
        rejects<TransportError>([&]{discover_mcp_oauth(tls+"/mcp",{},std::chrono::steady_clock::now()+5s);});
        std::cout<<"Native provider/MCP POST byte transport and OAuth metadata fixtures passed; TLS/pre-cancel/deadline discovery failures checked. No OAuth login/token exchange, live model or complete MCP support verified\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
