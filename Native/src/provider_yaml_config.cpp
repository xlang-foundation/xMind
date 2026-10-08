#include "agentflow/provider_yaml_config.hpp"
#include "yaml-cpp/parser.h"
#include "yaml-cpp/eventhandler.h"
#include "yaml-cpp/exceptions.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <streambuf>

namespace agentflow {
namespace {
constexpr std::size_t file_limit=256*1024,node_limit=512,scalar_limit=32768,depth_limit=8;
[[noreturn]] void fail(ProviderYamlErrorCode code){throw ProviderYamlError(code);}
void wipe(std::string& value)noexcept{volatile char* bytes=value.empty()?nullptr:value.data();for(std::size_t i=0;i<value.size();++i)bytes[i]=0;}
void utf8(std::string_view value){
    for(std::size_t i=0;i<value.size();){const auto first=static_cast<unsigned char>(value[i++]);
        if(!first)fail(ProviderYamlErrorCode::invalid_utf8);
        if(first<0x80)continue;unsigned remaining=0;std::uint32_t code=0,minimum=0;
        if(first>=0xc2&&first<=0xdf){remaining=1;code=first&0x1f;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){remaining=2;code=first&0x0f;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){remaining=3;code=first&0x07;minimum=0x10000;}
        else fail(ProviderYamlErrorCode::invalid_utf8);
        if(remaining>value.size()-i)fail(ProviderYamlErrorCode::invalid_utf8);
        while(remaining--){const auto next=static_cast<unsigned char>(value[i++]);if((next&0xc0)!=0x80)fail(ProviderYamlErrorCode::invalid_utf8);code=(code<<6)|(next&0x3f);}
        if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))fail(ProviderYamlErrorCode::invalid_utf8);
    }
}
void identity(const std::string& value){if(value.empty()||value.size()>256||value.starts_with("sk-")||value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:/-")!=std::string::npos)fail(ProviderYamlErrorCode::invalid_schema);}
bool text_tag(const std::string& tag){return tag=="?"||tag=="!"||tag=="tag:yaml.org,2002:str";}
struct Value {
    bool mapping=false;
    std::string scalar,tag;
    std::vector<std::pair<std::string,std::unique_ptr<Value>>> fields;
    ~Value(){wipe(scalar);}
};
class BoundedHandler final : public YAML::EventHandler {
    struct Frame {Value* value;std::optional<std::string> key;std::set<std::string> keys;};
    std::vector<Frame> stack_;
    std::size_t nodes_=0,documents_=0;
    std::unique_ptr<Value> root_;
    Value* attach(std::unique_ptr<Value> value){
        if(++nodes_>node_limit)fail(ProviderYamlErrorCode::exceeds_limits);
        auto* pointer=value.get();
        if(stack_.empty()){if(root_)fail(ProviderYamlErrorCode::invalid_schema);root_=std::move(value);}
        else {auto& frame=stack_.back();if(!frame.key)fail(ProviderYamlErrorCode::unsupported_yaml);frame.value->fields.emplace_back(std::move(*frame.key),std::move(value));frame.key.reset();}
        return pointer;
    }
public:
    void OnDocumentStart(const YAML::Mark&)override{if(++documents_!=1)fail(ProviderYamlErrorCode::unsupported_yaml);}
    void OnDocumentEnd()override{if(!stack_.empty()||!root_)fail(ProviderYamlErrorCode::invalid_schema);}
    void OnAnchor(const YAML::Mark&,const std::string&)override{fail(ProviderYamlErrorCode::unsupported_yaml);}
    void OnAlias(const YAML::Mark&,YAML::anchor_t)override{fail(ProviderYamlErrorCode::unsupported_yaml);}
    void OnNull(const YAML::Mark&,YAML::anchor_t)override{fail(ProviderYamlErrorCode::invalid_schema);}
    void OnSequenceStart(const YAML::Mark&,const std::string&,YAML::anchor_t,YAML::EmitterStyle::value)override{fail(ProviderYamlErrorCode::unsupported_yaml);}
    void OnSequenceEnd()override{fail(ProviderYamlErrorCode::unsupported_yaml);}
    void OnMapStart(const YAML::Mark&,const std::string& tag,YAML::anchor_t anchor,YAML::EmitterStyle::value)override{
        if(anchor!=YAML::NullAnchor||(!text_tag(tag)&&tag!="tag:yaml.org,2002:map"))fail(ProviderYamlErrorCode::unsupported_yaml);
        if(stack_.size()>=depth_limit)fail(ProviderYamlErrorCode::exceeds_limits);
        auto value=std::make_unique<Value>();value->mapping=true;auto* node=attach(std::move(value));stack_.push_back({node,{},{}});
    }
    void OnMapEnd()override{if(stack_.empty()||stack_.back().key)fail(ProviderYamlErrorCode::invalid_schema);stack_.pop_back();}
    void OnScalar(const YAML::Mark&,const std::string& tag,YAML::anchor_t anchor,const std::string& scalar)override{
        if(anchor!=YAML::NullAnchor||(!text_tag(tag)&&tag!="tag:yaml.org,2002:int"))fail(ProviderYamlErrorCode::unsupported_yaml);
        if(scalar.size()>scalar_limit)fail(ProviderYamlErrorCode::exceeds_limits);
        utf8(scalar);
        if(!stack_.empty()&&!stack_.back().key){auto& frame=stack_.back();if(++nodes_>node_limit||scalar.size()>256||!text_tag(tag))fail(ProviderYamlErrorCode::exceeds_limits);if(!frame.keys.insert(scalar).second)fail(ProviderYamlErrorCode::duplicate_field);frame.key=scalar;return;}
        auto value=std::make_unique<Value>();value->scalar=scalar;value->tag=tag;attach(std::move(value));
    }
    std::unique_ptr<Value> take(){return std::move(root_);}
};
class ViewBuffer final : public std::streambuf {
public:
    explicit ViewBuffer(std::string_view source){auto* first=const_cast<char*>(source.data());setg(first,first,first+source.size());}
};
const Value* field(const Value& value,const std::string& name){for(const auto& [key,node]:value.fields)if(key==name)return node.get();return nullptr;}
const std::string& text(const Value& value){if(value.mapping||!text_tag(value.tag))fail(ProviderYamlErrorCode::invalid_schema);return value.scalar;}
void fields(const Value& value,const std::set<std::string>& allowed){if(!value.mapping)fail(ProviderYamlErrorCode::invalid_schema);for(const auto& [key,node]:value.fields){(void)node;if(!allowed.contains(key))fail(ProviderYamlErrorCode::invalid_schema);}}
ProviderYamlConfig decode(const Value& root,const std::vector<std::string>& allowed_routes){
    if(allowed_routes.empty()||allowed_routes.size()>64)fail(ProviderYamlErrorCode::route_unavailable);std::set<std::string> routes;
    for(const auto& route:allowed_routes){identity(route);if(!routes.insert(route).second)fail(ProviderYamlErrorCode::route_unavailable);}
    fields(root,{"version","profiles","active_profile"});const auto* version=field(root,"version"),*profiles=field(root,"profiles");
    if(!version||version->mapping||version->scalar!="1"||(version->tag!="?"&&version->tag!="tag:yaml.org,2002:int")||!profiles||!profiles->mapping||profiles->fields.size()>32)fail(ProviderYamlErrorCode::invalid_schema);
    ProviderYamlConfig result;
    for(const auto& [id,node]:profiles->fields){identity(id);fields(*node,{"route","api_key","model"});const auto* route=field(*node,"route");if(!route)fail(ProviderYamlErrorCode::invalid_schema);const auto& route_id=text(*route);identity(route_id);if(!routes.contains(route_id))fail(ProviderYamlErrorCode::route_unavailable);
        ProviderProfileConfigEntry entry;entry.id=id;entry.route_id=route_id;
        if(const auto* model=field(*node,"model")){entry.model=text(*model);identity(*entry.model);}
        if(const auto* key=field(*node,"api_key")){const auto& scalar=text(*key);for(const auto byte:scalar)if(static_cast<unsigned char>(byte)<33||static_cast<unsigned char>(byte)>126)fail(ProviderYamlErrorCode::invalid_schema);entry.key=SecretBytes({reinterpret_cast<const std::uint8_t*>(scalar.data()),scalar.size()});}
        if(!entry.key.view().empty()){const auto bytes=entry.key.view();const auto reflects=[&](const std::string& value){return std::search(value.begin(),value.end(),bytes.begin(),bytes.end(),[](char a,std::uint8_t b){return static_cast<unsigned char>(a)==b;})!=value.end();};if(reflects(entry.id)||(entry.model&&reflects(*entry.model)))fail(ProviderYamlErrorCode::invalid_schema);}
        result.profiles.push_back(std::move(entry));
    }
    // A partial document may select an unspecified existing profile. Its actual
    // merged registry membership is checked by the atomic backend batch.
    if(const auto* active=field(root,"active_profile")){result.active_profile=text(*active);identity(*result.active_profile);}
    return result;
}
}
ProviderYamlError::ProviderYamlError(ProviderYamlErrorCode code):std::runtime_error("Provider YAML configuration rejected"),code_(code){}
ProviderYamlErrorCode ProviderYamlError::code()const noexcept{return code_;}
ProviderYamlConfig parse_provider_yaml_config(std::string_view source,const std::vector<std::string>& routes){
    if(source.empty()||source.size()>file_limit)fail(ProviderYamlErrorCode::exceeds_limits);
    utf8(source);
    try{ViewBuffer buffer(source);std::istream input(&buffer);YAML::Parser parser(input);BoundedHandler handler;if(!parser.HandleNextDocument(handler))fail(ProviderYamlErrorCode::invalid_schema);if(parser.HandleNextDocument(handler))fail(ProviderYamlErrorCode::unsupported_yaml);const auto root=handler.take();if(!root)fail(ProviderYamlErrorCode::invalid_schema);return decode(*root,routes);}
    catch(const ProviderYamlError&){throw;}
    catch(const YAML::Exception&){fail(ProviderYamlErrorCode::invalid_yaml);}
    catch(...){fail(ProviderYamlErrorCode::invalid_yaml);}
}
ProviderYamlConfig read_provider_yaml_config(const std::filesystem::path& path,const std::vector<std::string>& routes){
    struct Input {std::string bytes;~Input(){wipe(bytes);}} input;
    try{
        if(!path.is_absolute()||path.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos||!std::filesystem::is_regular_file(path))fail(ProviderYamlErrorCode::file_unavailable);
        std::ifstream stream(path,std::ios::binary);if(!stream)fail(ProviderYamlErrorCode::file_unavailable);
        input.bytes.resize(file_limit+1);stream.read(input.bytes.data(),static_cast<std::streamsize>(input.bytes.size()));const auto count=stream.gcount();if(count<0||stream.bad())fail(ProviderYamlErrorCode::file_unavailable);input.bytes.resize(static_cast<std::size_t>(count));
        if(input.bytes.size()>file_limit)fail(ProviderYamlErrorCode::exceeds_limits);
        return parse_provider_yaml_config(input.bytes,routes);
    }catch(const ProviderYamlError&){throw;}catch(...){fail(ProviderYamlErrorCode::file_unavailable);}
}
}
