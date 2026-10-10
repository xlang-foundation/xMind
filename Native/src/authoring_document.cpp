#include "agentflow/authoring_document.hpp"
#include "yaml-cpp/eventhandler.h"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/parser.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <streambuf>
#include <vector>

namespace agentflow {
namespace {
using Json=nlohmann::json;
constexpr std::size_t source_limit=262144,node_limit=8192,scalar_limit=32768,
                      map_key_limit=256,depth_limit=32;

class InputBuffer final:public std::streambuf {
public:
    explicit InputBuffer(std::string_view value){auto* begin=const_cast<char*>(value.data());setg(begin,begin,begin+value.size());}
};

struct Value {
    enum class Kind {scalar,map,sequence};
    Kind kind=Kind::scalar;
    Json scalar;
    std::vector<std::pair<std::string,std::unique_ptr<Value>>> fields;
    std::vector<std::unique_ptr<Value>> items;
};

std::string folded(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
bool text_tag(const std::string& tag){return tag=="?"||tag=="!"||tag=="tag:yaml.org,2002:str";}
[[noreturn]] void invalid(){throw std::invalid_argument("Invalid or unsupported YAML authoring document");}

Json scalar_value(const std::string& tag,const std::string& source){
    if(source.size()>scalar_limit||source.find('\0')!=std::string::npos)invalid();
    if(text_tag(tag)&&tag!="tag:yaml.org,2002:str"){
        const auto lower=folded(source);
        if(lower=="null"||source=="~")return nullptr;
        if(lower=="true")return true;
        if(lower=="false")return false;
        const bool looks_numeric=!source.empty()&&
            (std::isdigit(static_cast<unsigned char>(source.front()))||source.front()=='-'||source.front()=='+');
        if(looks_numeric){
            std::string number;number.reserve(source.size());
            for(const char c:source)if(c!='_')number.push_back(c);
            if(number.find_first_of(".eE")!=std::string::npos){
                auto json=Json::parse(number);if(!json.is_number()||!std::isfinite(json.get<double>()))invalid();return json;
            }
            if(number.front()=='+')number.erase(number.begin());
            auto json=Json::parse(number);
            if(!json.is_number_integer()||json.get<long double>()>9007199254740991.0L||json.get<long double>()< -9007199254740991.0L)invalid();
            return json;
        }
        return source;
    }
    if(tag=="tag:yaml.org,2002:int"||tag=="tag:yaml.org,2002:float"){
        std::string number;number.reserve(source.size());for(const char c:source)if(c!='_')number.push_back(c);
        if(number.starts_with("+"))number.erase(number.begin());
        auto json=Json::parse(number);if(!json.is_number())invalid();
        if(json.is_number_integer()&&(json.get<long double>()>9007199254740991.0L||json.get<long double>()< -9007199254740991.0L))invalid();
        if(json.is_number_float()&&!std::isfinite(json.get<double>()))invalid();return json;
    }
    if(tag=="tag:yaml.org,2002:bool"){
        const auto lower=folded(source);if(lower=="true")return true;if(lower=="false")return false;invalid();
    }
    if(tag=="tag:yaml.org,2002:null")return nullptr;
    if(tag=="tag:yaml.org,2002:binary"||tag=="tag:yaml.org,2002:timestamp")invalid();
    if(tag=="tag:yaml.org,2002:seq"||tag=="tag:yaml.org,2002:map")invalid();
    if(tag=="tag:yaml.org,2002:str")return source;
    invalid();
}

class BoundedDocument final:public YAML::EventHandler {
    struct Frame {
        Value* value=nullptr;
        std::optional<std::string> key;
        std::set<std::string> keys;
    };
    std::vector<Frame> stack_;
    std::unique_ptr<Value> root_;
    std::size_t nodes_=0,documents_=0;

    Value* attach(std::unique_ptr<Value> value){
        if(++nodes_>node_limit)invalid();auto* result=value.get();
        if(stack_.empty()){
            if(root_)invalid();root_=std::move(value);
        }else{
            auto& frame=stack_.back();
            if(frame.value->kind==Value::Kind::sequence)frame.value->items.push_back(std::move(value));
            else if(frame.value->kind==Value::Kind::map&&frame.key){frame.value->fields.emplace_back(std::move(*frame.key),std::move(value));frame.key.reset();}
            else invalid();
        }
        return result;
    }
    static bool simple_tag(const std::string& tag){
        return tag.empty()||tag=="?"||tag=="!"||tag=="tag:yaml.org,2002:seq"||tag=="tag:yaml.org,2002:map";
    }
public:
    void OnDocumentStart(const YAML::Mark&)override{if(++documents_!=1)invalid();}
    void OnDocumentEnd()override{if(!stack_.empty()||!root_)invalid();}
    void OnAnchor(const YAML::Mark&,const std::string&)override{invalid();}
    void OnAlias(const YAML::Mark&,YAML::anchor_t)override{invalid();}
    void OnNull(const YAML::Mark&,YAML::anchor_t anchor)override{
        if(anchor!=YAML::NullAnchor)invalid();auto node=std::make_unique<Value>();node->scalar=nullptr;attach(std::move(node));
    }
    void OnMapStart(const YAML::Mark&,const std::string& tag,YAML::anchor_t anchor,YAML::EmitterStyle::value)override{
        if(anchor!=YAML::NullAnchor||!simple_tag(tag)||stack_.size()>=depth_limit)invalid();
        auto node=std::make_unique<Value>();node->kind=Value::Kind::map;auto* ptr=attach(std::move(node));stack_.push_back({ptr,{},{}});
    }
    void OnMapEnd()override{if(stack_.empty()||stack_.back().value->kind!=Value::Kind::map||stack_.back().key)invalid();stack_.pop_back();}
    void OnSequenceStart(const YAML::Mark&,const std::string& tag,YAML::anchor_t anchor,YAML::EmitterStyle::value)override{
        if(anchor!=YAML::NullAnchor||!simple_tag(tag)||stack_.size()>=depth_limit)invalid();
        auto node=std::make_unique<Value>();node->kind=Value::Kind::sequence;auto* ptr=attach(std::move(node));stack_.push_back({ptr,{},{}});
    }
    void OnSequenceEnd()override{if(stack_.empty()||stack_.back().value->kind!=Value::Kind::sequence)invalid();stack_.pop_back();}
    void OnScalar(const YAML::Mark&,const std::string& tag,YAML::anchor_t anchor,const std::string& scalar)override{
        if(anchor!=YAML::NullAnchor||scalar.size()>scalar_limit)invalid();
        if(!stack_.empty()&&stack_.back().value->kind==Value::Kind::map&&!stack_.back().key){
            if(!text_tag(tag)||scalar.size()>map_key_limit||scalar.find('\0')!=std::string::npos)invalid();
            auto& frame=stack_.back();if(!frame.keys.insert(scalar).second)throw std::invalid_argument("Duplicate YAML authoring field");frame.key=scalar;return;
        }
        auto node=std::make_unique<Value>();node->scalar=scalar_value(tag,scalar);attach(std::move(node));
    }
    std::unique_ptr<Value> take(){return std::move(root_);}
};

Json convert(const Value& value){
    if(value.kind==Value::Kind::scalar)return value.scalar;
    if(value.kind==Value::Kind::sequence){auto result=Json::array();for(const auto& item:value.items)result.push_back(convert(*item));return result;}
    auto result=Json::object();for(const auto& [key,item]:value.fields)result[key]=convert(*item);return result;
}
}

std::string authoring_yaml_to_json(std::string_view source){
    if(source.empty()||source.size()>source_limit)throw std::invalid_argument("YAML authoring document exceeds limits");
    try{
        InputBuffer buffer(source);std::istream input(&buffer);YAML::Parser parser(input);BoundedDocument handler;
        if(!parser.HandleNextDocument(handler))invalid();if(parser.HandleNextDocument(handler))invalid();
        auto root=handler.take();if(!root)invalid();return convert(*root).dump();
    }catch(const std::invalid_argument&){throw;}
     catch(const YAML::Exception&){invalid();}
     catch(const Json::exception&){invalid();}
     catch(...){invalid();}
}

}
