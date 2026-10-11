#include "agentflow/language_server.hpp"
#include "agentflow/process.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>

namespace agentflow {
namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
std::string utf8(const std::filesystem::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
std::string file_uri(const std::filesystem::path& path){
    const auto absolute=std::filesystem::absolute(path).lexically_normal();auto raw=utf8(absolute);std::string uri="file:///";
    for(unsigned char c:raw){if(c=='\\'||c=='/')uri.push_back('/');else if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'||c==':')uri.push_back(static_cast<char>(c));else{constexpr char hex[]="0123456789ABCDEF";uri.push_back('%');uri.push_back(hex[c>>4]);uri.push_back(hex[c&15]);}}
    return uri;
}
std::string lower(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
std::string header_value(std::string_view headers,std::string_view wanted){
    std::size_t start=0;bool found=false;std::string result;
    while(start<headers.size()){
        const auto end=headers.find("\r\n",start);const auto stop=end==std::string_view::npos?headers.size():end;const auto line=headers.substr(start,stop-start);const auto colon=line.find(':');
        if(colon==std::string_view::npos)throw std::runtime_error("Invalid language-server header");
        if(lower(std::string(line.substr(0,colon)))==lower(std::string(wanted))){if(found)throw std::runtime_error("Duplicate language-server content length");found=true;auto value=line.substr(colon+1);while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.remove_prefix(1);result=std::string(value);}
        if(end==std::string_view::npos)break;start=end+2;
    }
    if(!found)throw std::runtime_error("Language-server frame has no content length");return result;
}
class LspChannel {
public:
    explicit LspChannel(McpStdioProcess& process):process_(process){}
    void send(const Json& message,Clock::time_point deadline,std::stop_token cancel){const auto body=message.dump();if(body.empty()||body.size()>1024*1024)throw std::invalid_argument("Language-server message exceeds limits");const auto header="Content-Length: "+std::to_string(body.size())+"\r\n\r\n";process_.write(header+body,deadline,cancel);}
    Json receive(Clock::time_point deadline,std::stop_token cancel){
        for(;;){
            const auto boundary=buffer_.find("\r\n\r\n");
            if(boundary!=std::string::npos){if(boundary>8192)throw std::runtime_error("Language-server header exceeds limits");const auto length_text=header_value(std::string_view(buffer_).substr(0,boundary),"Content-Length");std::size_t length=0;const auto parsed=std::from_chars(length_text.data(),length_text.data()+length_text.size(),length);if(length_text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=length_text.data()+length_text.size()||length>1024*1024)throw std::runtime_error("Invalid language-server content length");const auto body_start=boundary+4;if(buffer_.size()-body_start>=length){const auto body=buffer_.substr(body_start,length);buffer_.erase(0,body_start+length);return Json::parse(body);}}
            if(buffer_.size()>8192+1024*1024)throw std::runtime_error("Language-server receive buffer exceeds limits");
            const auto chunk=process_.read(deadline,cancel);if(!chunk){const auto status=process_.status();throw std::runtime_error(status.exit_code?"Language server exited with code "+std::to_string(*status.exit_code):"Language server closed its output (transport closed="+std::string(status.closed?"true":"false")+", faulted="+(status.faulted?"true":"false")+")");}buffer_+=*chunk;
        }
    }
private:McpStdioProcess& process_;std::string buffer_;
};
}
struct LanguageServerClient::Impl {
    WorkspaceTools& workspace;
    const LanguageServerProfile& profile;
    std::mutex mutex;
    std::unique_ptr<McpStdioProcess> process;
    std::unique_ptr<LspChannel> channel;
    std::map<std::string,std::pair<std::int64_t,std::string>> documents;
    std::map<std::string,Json> published_diagnostics;
    std::uint64_t next=1;
    bool initialized=false;
    bool document_formatting=false;

    Impl(WorkspaceTools& value,const LanguageServerProfile& setting):workspace(value),profile(setting){}

    Json exchange(const std::string& method,Json params,Clock::time_point deadline,std::stop_token cancel){
        for(unsigned attempt=0;attempt<4;++attempt){
            const auto id="xmind-"+std::to_string(next++);
            channel->send(Json{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}},deadline,cancel);
            for(;;){
                const auto message=channel->receive(deadline,cancel);
                if(!message.is_object())continue;
                if(message.value("method",std::string{})=="textDocument/publishDiagnostics"){
                    const auto payload=message.value("params",Json::object());const auto uri=payload.value("uri",std::string{});
                    if(!uri.empty())published_diagnostics[uri]=payload.value("diagnostics",Json::array());continue;
                }
                if(message.value("id",Json())==id){
                    if(message.contains("error")){
                        const auto& error=message["error"];
                        if(error.is_object()&&attempt<3&&(error.value("code",0)==-32800||error.value("code",0)==-32801||error.value("code",0)==-32802)){std::this_thread::sleep_for(std::chrono::milliseconds(50));break;}
                        std::string detail;
                        if(error.is_object()&&error.contains("message")&&error["message"].is_string())for(const unsigned char ch:error["message"].get<std::string>()){
                            if(detail.size()>=512)break;if(ch=='\r'||ch=='\n'||ch=='\t')detail.push_back(' ');else if(ch>=0x20&&ch!=0x7f)detail.push_back(static_cast<char>(ch));
                        }
                        throw std::runtime_error(detail.empty()?"Language server rejected the request":"Language server rejected the request: "+detail);
                    }
                    return message.value("result",Json());
                }
                if(message.contains("method")&&message.contains("id")){
                    const auto server_id=message["id"];const auto server_method=message.value("method",std::string{});Json response={{"jsonrpc","2.0"},{"id",server_id}};
                    if(server_method=="workspace/configuration"){
                        const auto requested=message.value("params",Json::object()).value("items",Json::array());Json result=Json::array();
                        if(requested.is_array())for(std::size_t i=0;i<requested.size();++i)result.push_back(Json::object());
                        response["result"]=std::move(result);
                    }else if(server_method=="workspace/applyEdit")response["result"]={{"applied",false},{"failureReason","Language servers cannot edit workspace files through this read-only client"}};
                    else if(server_method=="window/workDoneProgress/create"||server_method=="client/registerCapability"||server_method=="client/unregisterCapability"||server_method=="window/showMessageRequest")response["result"]=nullptr;
                    else response["error"]={{"code",-32601},{"message","Client request is not supported"}};
                    channel->send(response,deadline,cancel);
                }
            }
        }
        throw std::runtime_error("Language-server retry budget exhausted");
    }
    void start(Clock::time_point deadline,std::stop_token cancel){
        if(initialized)return;
        if(ForegroundProcess::executable_identity(profile.executable)!=profile.executable_id)throw ToolAccessDenied("Configured language-server executable changed after registration");
        // Rust language servers launch Cargo and compiler helpers while
        // indexing a project; allow that bounded child tree to start.
        McpStdioConfiguration config{profile.executable,workspace.root_path(),profile.arguments,profile.environment,4*1024*1024,512*1024*1024,std::chrono::seconds(45),32};
        process=std::make_unique<McpStdioProcess>(config);channel=std::make_unique<LspChannel>(*process);
        const auto root=std::filesystem::u8path(workspace.root_path());const auto root_uri=file_uri(root);
        const auto capabilities=exchange("initialize",Json{{"processId",nullptr},{"rootUri",root_uri},{"workspaceFolders",Json::array({{{"uri",root_uri},{"name",root.filename().string()}}})},{"capabilities",{{"textDocument",{{"hover",{{"contentFormat",Json::array({"plaintext","markdown"})}}},{"definition",{{"linkSupport",true}}},{"references",{{"dynamicRegistration",false}}},{"documentSymbol",{{"hierarchicalDocumentSymbolSupport",true}}},{"formatting",{{"dynamicRegistration",false}}}}}}},{"clientInfo",{{"name","xMind"},{"version","native"}}}} ,deadline,cancel);
        const auto formatting=capabilities.is_object()?capabilities.value("capabilities",Json::object()).value("documentFormattingProvider",Json()):Json();document_formatting=formatting.is_boolean()?formatting.get<bool>():formatting.is_object();
        channel->send(Json{{"jsonrpc","2.0"},{"method","initialized"},{"params",Json::object()}},deadline,cancel);initialized=true;
    }
    void open(const std::string& uri,const std::string& language,const std::string& path,const std::string& text,Clock::time_point deadline,std::stop_token cancel){
        const auto found=documents.find(uri);
        if(found==documents.end()){
            channel->send(Json{{"jsonrpc","2.0"},{"method","textDocument/didOpen"},{"params",{{"textDocument",{{"uri",uri},{"languageId",language},{"version",1},{"text",text}}}}}},deadline,cancel);
            documents.emplace(uri,std::make_pair(1,text));return;
        }
        if(found->second.second==text)return;
        if(found->second.first>=std::numeric_limits<std::int64_t>::max())throw std::runtime_error("Language-server document version exhausted");
        ++found->second.first;found->second.second=text;
        channel->send(Json{{"jsonrpc","2.0"},{"method","textDocument/didChange"},{"params",{{"textDocument",{{"uri",uri},{"version",found->second.first}}},{"contentChanges",Json::array({{{"text",text}}})}}}},deadline,cancel);
        (void)path;
    }
    void stop()noexcept{
        if(process){try{const auto deadline=Clock::now()+std::chrono::seconds(2);(void)exchange("shutdown",nullptr,deadline,{});channel->send(Json{{"jsonrpc","2.0"},{"method","exit"},{"params",nullptr}},deadline,{});}catch(...){}process->shutdown();}
        channel.reset();process.reset();initialized=false;document_formatting=false;documents.clear();published_diagnostics.clear();
    }
};

LanguageServerClient::LanguageServerClient(WorkspaceTools& workspace,LanguageServerProfile profile):workspace_(workspace),profile_(std::move(profile)){
    if(profile_.id.empty()||profile_.id.size()>64||profile_.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||profile_.language_id.empty()||profile_.language_id.size()>64||profile_.language_id.find('\0')!=std::string::npos||profile_.language_id.find_first_of("\r\n\t ")!=std::string::npos||profile_.executable.empty()||profile_.executable.find('\0')!=std::string::npos||profile_.extensions.empty()||profile_.extensions.size()>32||profile_.arguments.size()>32||profile_.environment.size()>64||profile_.request_timeout<std::chrono::seconds(1)||profile_.request_timeout>std::chrono::minutes(2))throw std::invalid_argument("Invalid language-server profile");
    for(const auto& argument:profile_.arguments)if(argument.size()>4096||argument.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid language-server argument");
    const auto executable=std::filesystem::u8path(profile_.executable);if(!executable.is_absolute()||!std::filesystem::is_regular_file(executable))throw std::invalid_argument("Language-server executable must be an existing absolute file");
    if(profile_.executable_id.empty())profile_.executable_id=ForegroundProcess::executable_identity(profile_.executable);else if(ForegroundProcess::executable_identity(profile_.executable)!=profile_.executable_id)throw std::invalid_argument("Language-server executable changed after registration");
    for(const auto& extension:profile_.extensions)if(extension.size()<2||extension.size()>16||extension.front()!='.'||extension.find_first_of("/\\")!=std::string::npos||extension.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid language-server file extension");
    impl_=std::make_unique<Impl>(workspace_,profile_);
}
LanguageServerClient::~LanguageServerClient(){if(impl_){std::lock_guard lock(impl_->mutex);impl_->stop();}}
std::string LanguageServerClient::request(const LanguageServerRequest& call,std::stop_token cancel)const{
    static const std::vector<std::string> methods{"hover","definition","references","documentSymbol","diagnostics"};
    if(call.method.empty()||(call.method!="formatting"&&std::find(methods.begin(),methods.end(),call.method)==methods.end()))throw std::invalid_argument("Unsupported language-server method");
    if((call.method=="formatting")!=call.formatting_options.has_value()||(call.formatting_options&&(call.formatting_options->tab_size<1||call.formatting_options->tab_size>16)))throw std::invalid_argument("Invalid language-server formatting options");
    const auto extension=lower(std::filesystem::u8path(call.path).extension().string());if(std::none_of(profile_.extensions.begin(),profile_.extensions.end(),[&](const auto& value){return lower(value)==extension;}))throw ToolAccessDenied("No configured language server owns this file type");
    auto file=workspace_.read_file(call.path,cancel);if(call.document_text)file.content=*call.document_text;if(file.content.size()>1024*1024)throw ToolFileError("Language-server document exceeds limits");
    std::lock_guard lock(impl_->mutex);const auto deadline=Clock::now()+profile_.request_timeout;
    const auto root=std::filesystem::u8path(workspace_.root_path());const auto uri=file_uri(root/std::filesystem::u8path(call.path));
    try{impl_->start(deadline,cancel);if(call.method=="formatting"&&!impl_->document_formatting)throw std::runtime_error("Configured language server does not support document formatting");impl_->open(uri,profile_.language_id,call.path,file.content,deadline,cancel);}
    catch(...){impl_->stop();throw;}
    try{
    Json params={{"textDocument",{{"uri",uri}}}};
    if(call.method=="hover"||call.method=="definition"||call.method=="references"){
        params["position"]={{"line",call.line},{"character",call.character}};
        if(call.method=="references")params["context"]={{"includeDeclaration",true}};
    }
    if(call.method=="formatting"){
        auto options=Json{{"tabSize",call.formatting_options->tab_size},{"insertSpaces",call.formatting_options->insert_spaces}};
        if(call.formatting_options->trim_trailing_whitespace)options["trimTrailingWhitespace"]=*call.formatting_options->trim_trailing_whitespace;
        if(call.formatting_options->insert_final_newline)options["insertFinalNewline"]=*call.formatting_options->insert_final_newline;
        if(call.formatting_options->trim_final_newlines)options["trimFinalNewlines"]=*call.formatting_options->trim_final_newlines;
        params["options"]=std::move(options);
    }
    const auto method=call.method=="documentSymbol"?"textDocument/documentSymbol":call.method=="diagnostics"?"textDocument/diagnostic":call.method=="formatting"?"textDocument/formatting":"textDocument/"+call.method;
    Json result;
    if(call.method=="diagnostics"){
        try{result=impl_->exchange(method,params,deadline,cancel);}catch(const std::runtime_error&){const auto found=impl_->published_diagnostics.find(uri);if(found!=impl_->published_diagnostics.end())result=Json{{"kind","full"},{"items",found->second}};else throw;}
    }else result=impl_->exchange(method,params,deadline,cancel);
    const auto diagnostic=impl_->published_diagnostics.find(uri);const auto diagnostics=diagnostic==impl_->published_diagnostics.end()?Json::array():diagnostic->second;
    return Json{{"server_id",profile_.id},{"language_id",profile_.language_id},{"path",file.path},{"method",call.method},{"result",std::move(result)},{"diagnostics",diagnostics}}.dump();
    }catch(...){impl_->stop();throw;}
}
}
