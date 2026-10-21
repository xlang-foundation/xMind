#pragma once
#include "xlang3/xlang3.h"
#include "nlohmann/json.hpp"
#include "agentflow/records.hpp"
namespace agentflow::sdk_values {
// Domain field adapters only. xlang3 IPC owns value serialization and transfer.
inline X::Value to_value(X3PackageHost* host,const nlohmann::json& value){
    using Json=nlohmann::json;
    if(value.is_null())return X::Value(nullptr);
    if(value.is_boolean())return X::Value(value.get<bool>());
    if(value.is_number_unsigned())return X::Value(value.get<std::uint64_t>());
    if(value.is_number_integer())return X::Value(value.get<std::int64_t>());
    if(value.is_number_float())return X::Value(value.get<double>());
    if(value.is_string())return X::Value::String(host,value.get<std::string>());
    if(value.is_array()){auto result=X::Value::List(host);for(const auto& item:value)if(!result.Append(to_value(host,item)))throw Conflict("Cannot create native list");return result;}
    if(value.is_object()){auto result=X::Value::Dict(host);for(auto it=value.begin();it!=value.end();++it)if(!result.SetItem(X::Value::String(host,it.key()),to_value(host,it.value())))throw Conflict("Cannot create native dictionary");return result;}
    throw Conflict("Unsupported native domain value");
}
struct ReadLimit {std::size_t nodes=0,bytes=0;};
inline nlohmann::json from_value(const X::Value& value,ReadLimit& limit,unsigned depth){
    using Json=nlohmann::json;
    if(depth>24||++limit.nodes>262144)throw Conflict("Native domain value exceeds bounds");
    if(value.IsNone())return nullptr;
    if(value.raw().tag==X3_TAG_BOOL)return value.ToInt64()!=0;
    if(value.IsInt64())return value.ToInt64();
    if(value.IsUInt64())return value.ToUInt64();
    if(value.IsDouble())return value.ToDouble();
    if(value.IsString()){auto text=value.ToString();limit.bytes+=text.size();if(limit.bytes>8*1024*1024)throw Conflict("Native text exceeds domain bounds");return text;}
    if(value.IsList()){if(value.Size()>262144)throw Conflict("Native list exceeds domain bounds");auto result=Json::array();for(std::uint64_t i=0;i<value.Size();++i)result.push_back(from_value(value[i],limit,depth+1));return result;}
    if(value.IsDict()){if(value.Size()>4096)throw Conflict("Native dictionary exceeds domain bounds");auto result=Json::object();for(std::uint64_t i=0;i<value.Size();++i){X::Value key,item;if(!value.DictEntry(i,key,item)||!key.IsString())throw Conflict("Native dictionary key differs");auto text=key.ToString();limit.bytes+=text.size();if(limit.bytes>8*1024*1024||result.contains(text))throw Conflict("Native dictionary exceeds domain bounds");result[text]=from_value(item,limit,depth+1);}return result;}
    throw Conflict("Unsupported native domain value");
}
inline nlohmann::json from_value(const X::Value& value){ReadLimit limit;return from_value(value,limit,0);}
}
