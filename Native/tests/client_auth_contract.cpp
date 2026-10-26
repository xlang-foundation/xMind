#include "agentflow/client_auth.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace agentflow;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string text(const fs::path& path){const auto s=path.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
SecretBytes secret(const std::string& value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
std::string reveal(const std::optional<SecretBytes>& value){return value?std::string(reinterpret_cast<const char*>(value->view().data()),value->view().size()):std::string{};}
template<class F>void rejects(F&& action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,message);}
struct Fixture {
    fs::path parent=fs::current_path(),root=parent/("client-auth-contract-"+std::to_string(GetCurrentProcessId()));
    Fixture(){check(root.is_absolute()&&root.parent_path()==parent&&!fs::exists(root),"Fixture root must be fresh and bounded");fs::create_directory(root);}
    ~Fixture(){if(root.is_absolute()&&root.parent_path()==parent){std::error_code ignored;fs::remove_all(root,ignored);}}
};
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"Expected xlang3 module and library directories\n";return 2;}
    try{
        Fixture fixture;const std::vector<std::string> roots{argv[1],argv[2]};
        const auto directory=fixture.root/L"vault",other=fixture.root/L"other";
        const auto vault=text(directory),other_vault=text(other);
        const auto dbfile=directory/L"client-auth.sqlite";
        const std::string origin="http://127.0.0.1:8765",next="http://127.0.0.1:8766",one(64,'a'),two(64,'b');
        check(!load_client_auth(vault,roots,origin)&&!fs::exists(directory),"Missing reads must not create storage");
        save_client_auth(vault,roots,origin,secret(one));
        check(reveal(load_client_auth(vault,roots,origin))==one,"Fresh runtime must restore the encrypted token");
        check(!load_client_auth(vault,roots,next),"Another origin must not receive a token");
        save_client_auth(vault,roots,origin,secret(two));
        save_client_auth(vault,roots,origin,secret(one),false);
        check(reveal(load_client_auth(vault,roots,origin))==two,"A stale seed must not replace a rotated token");
        {
            std::ifstream stream(dbfile,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(stream),{}};
            check(bytes.find(one)==std::string::npos&&bytes.find(two)==std::string::npos,"SQLite must not contain plaintext credentials");
        }
        rejects([&]{save_client_auth(vault,roots,"http://localhost:8765",secret(one));},"Noncanonical origins must be rejected");
        rejects([&]{save_client_auth(vault,roots,"http://127.0.0.1:80",secret(one));},"Explicit default ports must be canonicalized before storage");
        rejects([&]{save_client_auth(vault,roots,origin,secret("short"));},"Malformed tokens must not replace stored credentials");
        check(reveal(load_client_auth(vault,roots,origin))==two,"Rejected writes must preserve stored credentials");
        fs::create_hard_link(dbfile,fixture.root/L"alias.sqlite");
        rejects([&]{(void)load_client_auth(vault,roots,origin);},"Aliased database files must fail closed");
        fs::remove(fixture.root/L"alias.sqlite");
        {
            XlangSqlite db(text(dbfile),roots);
            const auto rows=db.execute("SELECT protection,ciphertext FROM client_auth WHERE origin=?",{origin}).rows;
            db.execute("INSERT INTO client_auth VALUES(?,?,?)",{next,rows.at(0).at(0),rows.at(0).at(1)});
        }
        rejects([&]{(void)load_client_auth(vault,roots,next);},"Ciphertext copied to another origin must not decrypt");
        delete_client_auth(vault,roots,next);
        check(reveal(load_client_auth(vault,roots,origin))==two,"A bad neighboring row must not change the valid origin");
        save_client_auth(other_vault,roots,origin,secret(one));
        fs::copy_file(dbfile,other/L"client-auth.sqlite",fs::copy_options::overwrite_existing);
        rejects([&]{(void)load_client_auth(other_vault,roots,origin);},"Ciphertext copied to another private directory must not decrypt");
        delete_client_auth(vault,roots,origin);
        check(!load_client_auth(vault,roots,origin),"Revocation must persist without returning stale credentials");
        std::cout<<"Native encrypted client authentication contract passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
