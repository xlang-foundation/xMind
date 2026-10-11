#include "agentflow/workspace_registry.hpp"
#include "agentflow/workspace_tools.hpp"
#include "agentflow/xlang_sqlite.hpp"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace agentflow {
namespace {
using namespace std::string_literals;
namespace fs=std::filesystem;

std::string text(const fs::path& path){
    const auto encoded=path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()),encoded.size()};
}
std::string column_text(const SqlValue& value){
    if(const auto* result=std::get_if<std::string>(&value))return *result;
    throw std::runtime_error("Workspace registry returned a non-text column");
}
std::int64_t column_integer(const SqlValue& value){
    if(const auto* result=std::get_if<std::int64_t>(&value))return *result;
    throw std::runtime_error("Workspace registry returned a non-integer column");
}
void validate_name(const std::string& name){
    if(name.empty()||name.size()>1024||name.find('\0')!=std::string::npos||
       std::any_of(name.begin(),name.end(),[](unsigned char c){return c<0x20||c==0x7f;}))
        throw std::invalid_argument("Workspace name must be 1..1024 UTF-8 bytes without control characters");
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0)<=0)
        throw std::invalid_argument("Workspace name must contain valid UTF-8");
}
bool path_prefix(const std::string& parent,const std::string& child){
    if(parent.empty()||child.size()<parent.size())return false;
    const auto a=std::filesystem::u8path(parent).native(),b=std::filesystem::u8path(child).native();
    if(CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(a.size()),TRUE)!=CSTR_EQUAL)return false;
    if(a.size()==b.size())return true;
    return b[a.size()]==L'\\'||b[a.size()]==L'/';
}
void reject_overlap(const std::string& first,const std::string& second){
    if(path_prefix(first,second)||path_prefix(second,first))
        throw std::invalid_argument("Workspace folders cannot overlap or contain one another");
}
std::string folder_name(const std::string& root){
    const auto name=text(fs::u8path(root).filename());
    return name.empty()?root:name;
}
std::int64_t unix_milliseconds(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
void schema(XlangSqlite& db){
    db.execute("CREATE TABLE IF NOT EXISTS workspace_groups("
        "workspace_id TEXT PRIMARY KEY NOT NULL,name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 1024),"
        "revision INTEGER NOT NULL CHECK(revision>0),primary_folder_id TEXT NOT NULL,created_ms INTEGER NOT NULL)");
    db.execute("CREATE TABLE IF NOT EXISTS workspace_folders("
        "workspace_id TEXT NOT NULL,folder_id TEXT NOT NULL,identity TEXT NOT NULL,root TEXT NOT NULL,"
        "name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 1024),ordinal INTEGER NOT NULL CHECK(ordinal>=0),"
        "PRIMARY KEY(workspace_id,folder_id),UNIQUE(workspace_id,identity),UNIQUE(workspace_id,ordinal),"
        "FOREIGN KEY(workspace_id) REFERENCES workspace_groups(workspace_id) ON DELETE CASCADE)");
    db.execute("CREATE INDEX IF NOT EXISTS workspace_folders_identity ON workspace_folders(identity)");
}
WorkspaceGroupRecord read_group(XlangSqlite& db,const std::string& id){
    const auto group=db.execute("SELECT workspace_id,name,revision,primary_folder_id FROM workspace_groups WHERE workspace_id=?",{id}).rows;
    if(group.empty())throw std::out_of_range("Workspace is not registered");
    if(group.size()!=1||group.front().size()!=4)throw std::runtime_error("Workspace registry group row is invalid");
    WorkspaceGroupRecord result;
    result.workspace_id=column_text(group.front()[0]);result.name=column_text(group.front()[1]);
    result.revision=column_integer(group.front()[2]);result.primary_folder_id=column_text(group.front()[3]);
    const auto rows=db.execute("SELECT folder_id,identity,root,name,ordinal FROM workspace_folders WHERE workspace_id=? ORDER BY ordinal",{id}).rows;
    if(rows.empty()||rows.size()>256)throw std::runtime_error("Workspace registry membership is empty or exceeds its limit");
    for(const auto& row:rows){
        if(row.size()!=5)throw std::runtime_error("Workspace registry folder row is invalid");
        result.folders.push_back({column_text(row[0]),column_text(row[1]),column_text(row[2]),column_text(row[3]),column_integer(row[4])});
    }
    if(result.folders.front().folder_id!=result.primary_folder_id||result.folders.front().ordinal!=0||result.revision<1)
        throw std::runtime_error("Workspace registry primary-folder invariant failed");
    for(std::size_t i=0;i<result.folders.size();++i){
        if(result.folders[i].ordinal!=static_cast<std::int64_t>(i)||result.folders[i].folder_id.empty()||result.folders[i].identity.empty()||result.folders[i].root.empty())
            throw std::runtime_error("Workspace registry folder ordering invariant failed");
        for(std::size_t j=0;j<i;++j)reject_overlap(result.folders[j].root,result.folders[i].root);
    }
    return result;
}
}

WorkspaceRegistry::WorkspaceRegistry(std::string database,std::vector<std::string> roots,std::string private_root)
    :database_(std::move(database)),private_storage_root_(std::move(private_root)),xlang_import_roots_(std::move(roots)){
    if(database_.empty()||database_.find('\0')!=std::string::npos||private_storage_root_.empty()||
       xlang_import_roots_.empty()||std::any_of(xlang_import_roots_.begin(),xlang_import_roots_.end(),[](const auto& root){return root.empty();}))
        throw std::invalid_argument("Invalid local workspace registry configuration");
}
WorkspaceGroupRecord WorkspaceRegistry::create(const std::string& input,const std::string& requested_name)const{
    WorkspaceTools folder(input);
    const auto root=folder.root_path(),identity=folder.identity();
    reject_overlap(private_storage_root_,root);
    const auto name=requested_name.empty()?folder_name(root):requested_name;validate_name(name);
    XlangSqlite db(database_,xlang_import_roots_);schema(db);
    const auto existing=db.execute("SELECT primary_folder_id FROM workspace_groups WHERE workspace_id=?",{identity}).rows;
    if(!existing.empty()){
        if(existing.size()!=1||existing.front().size()!=1||column_text(existing.front().front())!=identity)
            throw std::runtime_error("Existing workspace registry primary binding is invalid");
        return read_group(db,identity);
    }
    db.begin();
    try{
        db.execute("INSERT INTO workspace_groups(workspace_id,name,revision,primary_folder_id,created_ms) VALUES(?,?,1,?,?)",{identity,name,identity,unix_milliseconds()});
        db.execute("INSERT INTO workspace_folders(workspace_id,folder_id,identity,root,name,ordinal) VALUES(?,?,?,?,?,0)",{identity,identity,identity,root,folder_name(root)});
        db.commit();
    }catch(...){try{db.rollback();}catch(...){}throw;}
    return read_group(db,identity);
}
WorkspaceGroupRecord WorkspaceRegistry::add_folder(const std::string& id,const std::string& input)const{
    WorkspaceTools folder(input);
    const auto root=folder.root_path(),identity=folder.identity();
    reject_overlap(private_storage_root_,root);
    XlangSqlite db(database_,xlang_import_roots_);schema(db);db.begin();
    try{
        const auto group=read_group(db,id);
        if(group.folders.size()>=256)throw std::length_error("Workspace exceeds its 256-folder limit");
        for(const auto& member:group.folders){
            if(member.identity==identity)throw std::invalid_argument("Folder is already attached to this workspace");
            reject_overlap(member.root,root);
        }
        const auto ordinal=static_cast<std::int64_t>(group.folders.size());
        const auto added=db.execute("INSERT INTO workspace_folders(workspace_id,folder_id,identity,root,name,ordinal) VALUES(?,?,?,?,?,?)",
            {id,identity,identity,root,folder_name(root),ordinal});
        if(added.affected_rows!=1)throw std::runtime_error("Workspace folder attachment was not recorded");
        const auto changed=db.execute("UPDATE workspace_groups SET revision=revision+1 WHERE workspace_id=? AND revision=?",{id,group.revision});
        if(changed.affected_rows!=1)throw std::runtime_error("Workspace membership revision changed unexpectedly");
        db.commit();
    }catch(...){try{db.rollback();}catch(...){}throw;}
    return read_group(db,id);
}
std::vector<WorkspaceGroupRecord> WorkspaceRegistry::list()const{
    XlangSqlite db(database_,xlang_import_roots_);schema(db);
    const auto rows=db.execute("SELECT workspace_id FROM workspace_groups ORDER BY created_ms,workspace_id").rows;
    if(rows.size()>4096)throw std::length_error("Workspace registry exceeds its 4096-group limit");
    std::vector<WorkspaceGroupRecord> result;result.reserve(rows.size());
    for(const auto& row:rows){if(row.size()!=1)throw std::runtime_error("Workspace registry index row is invalid");result.push_back(read_group(db,column_text(row.front())));}
    return result;
}
WorkspaceGroupRecord WorkspaceRegistry::get(const std::string& id)const{
    XlangSqlite db(database_,xlang_import_roots_);schema(db);db.begin();
    try{auto result=read_group(db,id);db.commit();return result;}
    catch(...){try{db.rollback();}catch(...){}throw;}
}
}
