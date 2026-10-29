#pragma once
#include <charconv>
#include <optional>
#include <string_view>
namespace agentflow {
// Decode only our fixed WinHTTP messages; never publish arbitrary exception text.
inline std::optional<unsigned> provider_native_transport_error(std::string_view message){
    for(const std::string_view prefix:{"Native provider request failed (code ","Native HTTP operation failed (code "}){
        if(!message.starts_with(prefix)||!message.ends_with(')'))continue;
        const auto digits=message.substr(prefix.size(),message.size()-prefix.size()-1);
        if(digits.size()!=5)continue;unsigned code=0;
        const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),code);
        if(result.ec==std::errc{}&&result.ptr==digits.data()+digits.size()&&code>=12000&&code<=12200)return code;
    }
    return std::nullopt;
}
}
