#include "agentflow/xlang_sqlite.hpp"
#include "agentflow/records.hpp"
#include "xlang3/xlang3.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <thread>
#include <type_traits>

namespace agentflow {
namespace {
std::string sqlite_database_path(const std::string& input){
#if defined(_WIN32)
    if(input==":memory:")return input;
    auto path=std::filesystem::u8path(input);auto native=path.native();
    if(!path.is_absolute()||native.size()<252||native.starts_with(L"\\\\?\\")||native.starts_with(L"\\\\.\\"))return input;
    std::replace(native.begin(),native.end(),L'/',L'\\');
    const bool drive=native.size()>=3&&((native[0]>=L'A'&&native[0]<=L'Z')||(native[0]>=L'a'&&native[0]<=L'z'))&&native[1]==L':'&&native[2]==L'\\';
    const bool unc=native.starts_with(L"\\\\");if(!drive&&!unc)return input;
    for(std::size_t offset=drive?3:2;offset<native.size();){
        const auto end=native.find(L'\\',offset);const auto part=native.substr(offset,end==std::wstring::npos?native.size()-offset:end-offset);
        if(part.empty()||part==L"."||part==L".."||part.back()==L'.'||part.back()==L' '||part.find_first_of(L"<>:\"|?*")!=std::wstring::npos)return input;
        auto stem=part.substr(0,part.find(L'.'));for(auto& character:stem)if(character>=L'a'&&character<=L'z')character-=L'a'-L'A';
        if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL"||stem==L"CONIN$"||stem==L"CONOUT$"||
           (stem.size()==4&&(stem.starts_with(L"COM")||stem.starts_with(L"LPT"))&&
            ((stem[3]>=L'1'&&stem[3]<=L'9')||stem[3]==0x00b9||stem[3]==0x00b2||stem[3]==0x00b3)))return input;
        if(end==std::wstring::npos)break;offset=end+1;
    }
    const auto extended=drive?std::filesystem::path(L"\\\\?\\"+native):std::filesystem::path(L"\\\\?\\UNC\\"+native.substr(2));
    const auto encoded=extended.u8string();return {reinterpret_cast<const char*>(encoded.data()),encoded.size()};
#else
    return input;
#endif
}
}
struct XlangSqlite::Impl {
    // Runtime must outlive its connection and every temporary package value.
    X::Runtime runtime;
    X::Value connection;
    std::thread::id owner=std::this_thread::get_id();
    bool broken=false;
    ~Impl() {
        if(owner!=std::this_thread::get_id()) std::terminate();
        if(connection.IsValid()) {
            X::Value ignored;
            connection["close"].Call(std::vector<X::Value>{},ignored);
        }
    }
    void require_owner() const {
        if(owner!=std::this_thread::get_id()) throw std::logic_error("SQLite runtime belongs to its persistence thread");
    }
    X::Value call(const X::Value& method, const std::vector<X::Value>& args) {
        if(!method.IsValid()) throw DatabaseError("xlang3 SQLite method is unavailable");
        X::Value result;
        if(!method.Call(args,result)) throw DatabaseError(runtime.LastError());
        return result;
    }
    X::Value attribute(const X::Value& object,const char* name) {
        auto result=object[name];
        if(!result.IsValid()) throw DatabaseError(runtime.LastError());
        return result;
    }
    X::Value parameter(const SqlValue& input) {
        return std::visit([&](const auto& value)->X::Value {
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,std::nullptr_t>) return X::Value(nullptr);
            else if constexpr(std::is_same_v<T,SqlBytes>) {
                return X::Value(runtime,x3_value_bytes(runtime.get(),value.data(),value.size()));
            } else return X::Value(runtime,value);
        },input);
    }
    std::uint64_t length(const X::Value& value) {
        std::uint64_t result=0;
        runtime.check(x3_len(runtime.get(),value.raw(),&result));
        return result;
    }
    X::Value item(const X::Value& collection,std::uint64_t index) {
        if(index>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) throw std::length_error("SQLite result too large");
        X::Value key(runtime,static_cast<std::int64_t>(index));
        X3Value result=x3_value_invalid();
        runtime.check(x3_get_item(runtime.get(),collection.raw(),key.raw(),&result));
        return X::Value(runtime,result);
    }
    SqlValue copy(const X::Value& value) {
        if(value.IsNone()) return nullptr;
        if(value.IsInt64()) return value.ToInt64();
        if(value.IsDouble()) return value.ToDouble();
        if(value.IsString()) return value.ToString();
        const void* bytes=nullptr; std::uint64_t size=0;
        runtime.check(x3_value_bytes_data(runtime.get(),value.raw(),&bytes,&size));
        if(size>std::numeric_limits<std::size_t>::max()) throw std::length_error("SQLite blob too large");
        SqlBytes result(static_cast<std::size_t>(size));
        if(size) std::memcpy(result.data(),bytes,result.size());
        return result;
    }
};
XlangSqlite::XlangSqlite(const std::string& database,const std::vector<std::string>& roots) : impl_(std::make_unique<Impl>()) {
    if(database.empty() || database.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid SQLite path");
    for(const auto& root:roots) impl_->runtime.AddImportRoot(root);
    // The SDK exports its native connection API as _sqlite3. Importing the
    // pure sqlite3 facade must not depend on a host-installed standard library.
    X::Module sqlite(impl_->runtime,"_sqlite3");
    std::vector<X::Value> args{X::Value(impl_->runtime,sqlite_database_path(database))};
    const std::vector<std::pair<std::string,X::Value>> options{{"isolation_level",X::Value(nullptr)}};
    if(!sqlite["connect"].Call(args,options,impl_->connection)) throw DatabaseError(impl_->runtime.LastError());
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=5000");
}
XlangSqlite::~XlangSqlite()=default;
SqlResult XlangSqlite::execute(const std::string& sql,const std::vector<SqlValue>& parameters) {
    impl_->require_owner();
    if(impl_->broken) throw DatabaseError("SQLite connection requires replacement after failed rollback");
    if(sql.empty() || sql.find('\0')!=std::string::npos) throw std::invalid_argument("Invalid SQL text");
    auto args=impl_->runtime.List();
    for(const auto& value:parameters) {
        const auto item=impl_->parameter(value);
        if(!item.IsValid()) throw DatabaseError(impl_->runtime.LastError());
        if(!args.Append(item)) throw DatabaseError(impl_->runtime.LastError());
    }
    auto cursor=impl_->call(impl_->attribute(impl_->connection,"execute"),{X::Value(impl_->runtime,sql),args});
    SqlResult result;
    const auto count=impl_->attribute(cursor,"rowcount");
    if(!count.IsInt64()) throw DatabaseError("Invalid SQLite affected-row result");
    result.affected_rows=count.ToInt64(); // Preserve -1; never conceal native classification failures.
    const auto inserted=impl_->attribute(cursor,"lastrowid");
    if(!inserted.IsNone()) {
        if(!inserted.IsInt64()) throw DatabaseError("Invalid SQLite insert ID");
        result.last_insert_id=inserted.ToInt64();
    }
    if(!impl_->attribute(cursor,"description").IsNone()) {
        auto rows=impl_->call(impl_->attribute(cursor,"fetchall"),{});
        const auto size=impl_->length(rows);
        for(std::uint64_t i=0;i<size;++i) {
            const auto row=impl_->item(rows,i); std::vector<SqlValue> output;
            const auto columns=impl_->length(row);
            for(std::uint64_t j=0;j<columns;++j) output.push_back(impl_->copy(impl_->item(row,j)));
            result.rows.push_back(std::move(output));
        }
    }
    return result;
}
void XlangSqlite::begin() { execute("BEGIN IMMEDIATE"); }
void XlangSqlite::commit() { execute("COMMIT"); }
void XlangSqlite::rollback() {
    impl_->require_owner();
    try { execute("ROLLBACK"); }
    catch(...) {impl_->broken=true; throw;}
}
}
