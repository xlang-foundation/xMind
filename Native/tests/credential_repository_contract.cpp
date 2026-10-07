#include "agentflow/repository.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <system_error>

using namespace agentflow;
namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class Error,class Function> void rejects(Function action) {
    try {action();} catch(const Error&) {return;} throw std::runtime_error("Expected rejection did not occur");
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        std::random_device random;
        for(int i=0;i<32;++i) {
            auto candidate=std::filesystem::temp_directory_path()/("xmind-credentials-"+std::to_string(random())+"-"+std::to_string(random()));
            if(std::filesystem::create_directory(candidate)) {path=std::move(candidate);return;}
        }
        throw std::runtime_error("Cannot create test directory");
    }
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
bool same(const SecretBytes& secret,const SqlBytes& expected) {
    return std::ranges::equal(secret.view(),expected);
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {
        Directory directory;const auto path=(directory.path/"state.sqlite").string();
        const std::vector<std::string> roots{argv[1],argv[2]};
        const SqlBytes original{0,1,2,0,255,34,92,128,17},rotated{42,0,98,255,32};
        {
            Repository first(path,roots),second(path,roots);XlangSqlite inspect(path,roots);
            first.create_session("session","retained");
            first.append_message("session","user",R"({"text":"public"})");
            require(first.put_credential("team-a","model","provider:model","Label",SecretBytes(original),0).revision==1,"Creation revision");
            require(same(second.resolve_credential("team-a","model","provider:model"),original),"Binary secret round trip");
            require(second.credentials("team-b").empty(),"Scope metadata isolation");
            rejects<NotFound>([&]{second.resolve_credential("team-b","model","provider:model");});
            rejects<Conflict>([&]{second.resolve_credential("team-a","model","connector:mcp");});
            rejects<Conflict>([&]{second.put_credential("team-a","model","provider:model","duplicate",SecretBytes(rotated),0);});
            require(second.credentials("team-a").at(0).label=="Label","Rejected create must retain metadata");
            rejects<std::invalid_argument>([&]{first.put_credential("team-a","empty","provider:model","Empty",SecretBytes({}),0);});
            require(first.credentials("team-a").size()==1,"Empty secret must not add a record");
            const auto raw=inspect.execute("SELECT ciphertext FROM credentials").rows;
            const auto ciphertext=std::get<SqlBytes>(raw[0][0]);
            require(ciphertext!=original,"Stored value must be protected bytes");
            rejects<NotFound>([&]{first.information("credentials","model");});
            require(first.history("session").size()==1,"Credential mutation must not add conversation entries");
            first.put_credential("team-a","model","provider:model","Rotated",SecretBytes(rotated),1);
            rejects<Conflict>([&]{second.put_credential("team-a","model","provider:model","stale",SecretBytes(original),1);});
            rejects<Conflict>([&]{second.delete_credential("team-a","model",1);});
            require(same(second.resolve_credential("team-a","model","provider:model"),rotated),"Stale mutation must preserve rotated value");
            inspect.execute("CREATE TRIGGER reject_credential BEFORE UPDATE ON credentials BEGIN SELECT RAISE(ABORT,'fault'); END");
            rejects<DatabaseError>([&]{first.put_credential("team-a","model","provider:model","failed",SecretBytes(original),2);});
            require(second.credentials("team-a").at(0).revision==2,"Rejected storage update must roll back revision");
            require(same(second.resolve_credential("team-a","model","provider:model"),rotated),"Rejected storage update must retain ciphertext");
            inspect.execute("DROP TRIGGER reject_credential");
            // Copying another identity's ciphertext must fail context authentication.
            first.put_credential("team-b","model","provider:model","B",SecretBytes(original),0);
            inspect.execute("UPDATE credentials SET ciphertext=? WHERE scope='team-b'",{ciphertext});
            rejects<std::system_error>([&]{second.resolve_credential("team-b","model","provider:model");});
            first.delete_credential("team-b","model",1);
        }
        {
            Repository reopened(path,roots);
            require(same(reopened.resolve_credential("team-a","model","provider:model"),rotated),"Credential must survive reopen");
            require(reopened.credentials("team-a").at(0).revision==2,"Revision must survive reopen");
            reopened.delete_credential("team-a","model",2);
            rejects<NotFound>([&]{reopened.resolve_credential("team-a","model","provider:model");});
            require(reopened.credentials("team-a").empty(),"Deleted credential must leave no metadata");
            rejects<Conflict>([&]{reopened.put_credential("team-a","model","provider:model","reused",SecretBytes(original),0);});
        }
        // Reconstruct the previous schema, then verify migration to the current schema.
        {
            XlangSqlite previous(path,roots);
            previous.execute("DROP TABLE operation_resources");previous.execute("DROP TABLE operations");previous.execute("DROP TABLE credentials");previous.execute("DROP TABLE retired_credentials");previous.execute("PRAGMA user_version=1");
            previous.execute("CREATE TABLE credentials(unexpected TEXT)");
        }
        rejects<DatabaseError>([&]{Repository rejected(path,roots);});
        {
            XlangSqlite previous(path,roots);
            require(std::get<std::int64_t>(previous.execute("PRAGMA user_version").rows[0][0])==1,"Failed migration must not advance version");
            previous.execute("DROP TABLE credentials");
        }
        {
            Repository migrated(path,roots);
            require(migrated.history("session").size()==1,"Migration must preserve messages");
            migrated.put_credential("team-a","model","provider:model","Migrated",SecretBytes(original),0);
            require(same(migrated.resolve_credential("team-a","model","provider:model"),original),"Migrated credentials must work");
        }
        std::cout<<"Encrypted credential repository contracts passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
