#include "agentflow/agent_instructions.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <iostream>
using namespace agentflow;using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected instruction rejection did not occur");}
}
int main(int argc,char** argv){if(argc!=4)return 2;try{
    const auto database=(std::filesystem::u8path(argv[1])/"instructions.sqlite").string();const std::vector<std::string> imports{argv[2],argv[3]};
    {
        PersistenceService store(database,imports);AgentInstructionStore configuration(store);const auto absent=configuration.load();require(absent.revision==0&&absent.instructions.empty(),"Fresh storage must not invent an instruction policy");
        const std::string text="General and coding fixture instructions 🌍\nKeep actual tool evidence.";const auto first=configuration.apply(Json{{"instructions",text}}.dump());require(first.revision==1&&first.instructions==text,"Actual Unicode policy must receive a backend revision");require(configuration.apply(Json{{"instructions",text}}.dump()).revision==1,"Unchanged exact bytes must preserve revision");
        const auto saved=store.information("native-agent","instructions").get();
        for(const auto source:{"{}","[]",R"({"instructions":1})",R"({"instructions":"x","revision":99})",R"({"instructions":"x","version":1})",R"({"instructions":"x","credential_id":"spoof"})",R"({"instructions":"x","instructions":"y"})",R"({"instructions":"x\u0000y"})"}){rejects<std::invalid_argument>([&]{configuration.apply(source);});require(store.information("native-agent","instructions").get()==saved,"Rejected import must preserve exact saved record");}
        rejects<std::invalid_argument>([&]{configuration.apply(Json{{"instructions",std::string(32769,'x')}}.dump());});
        rejects<std::invalid_argument>([&]{configuration.apply(std::string("{\"instructions\":\"")+char(0xff)+"\"}");});
        {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER reject_instruction_update BEFORE UPDATE ON information WHEN NEW.category='native-agent' AND NEW.id='instructions' BEGIN SELECT RAISE(ABORT,'actual fixture instruction storage failure'); END");}
        rejects<DatabaseError>([&]{configuration.apply(R"({"instructions":"changed policy"})");});require(store.information("native-agent","instructions").get()==saved,"Actual SQLite failed update must retain the earlier policy");
        {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER reject_instruction_update");}
        const auto boundary=configuration.apply(Json{{"instructions",std::string(32768,'x')}}.dump());require(boundary.revision==2&&boundary.instructions.size()==32768,"Exact byte limit must be usable");
        require(configuration.apply(R"({"instructions":""})").revision==3,"Explicit empty supplement must rotate revision without changing core policy");store.close();
    }
    {
        PersistenceService store(database,imports);AgentInstructionStore configuration(store);const auto reopened=configuration.load();require(reopened.revision==3&&reopened.instructions.empty(),"Actual SQLite reopen must retain disabled supplement revision");
        store.put_information("native-agent","instructions",R"({"version":2,"revision":3,"instructions":"invalid stored version"})").get();const auto corrupt=store.information("native-agent","instructions").get();rejects<std::invalid_argument>([&]{configuration.load();});rejects<std::invalid_argument>([&]{configuration.apply(R"({"instructions":"do not overwrite corrupt history"})");});require(store.information("native-agent","instructions").get()==corrupt,"Unsupported stored version must fail closed and preserve evidence");
        store.put_information("native-agent","instructions",R"({"version":1,"revision":9007199254740991,"instructions":"last revision"})").get();rejects<std::overflow_error>([&]{configuration.apply(R"({"instructions":"overflow"})");});require(configuration.load().revision==9007199254740991,"Revision exhaustion must not wrap or overwrite");store.close();
    }
    std::cout<<"Native instruction configuration passed actual embedded-xlang3 Unicode persistence, backend revisions, malformed/spoofed/oversized imports, SQLite update failure preservation, empty supplement, reopen and corrupt/exhausted revision rejection. Model delivery is a separate integration contract.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
