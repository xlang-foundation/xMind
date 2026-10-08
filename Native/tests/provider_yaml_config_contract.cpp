// Native yaml-cpp parser + real encrypted profile registry/xlang3 SQLite.
// Keys, catalogue identities and source YAML below are explicitly synthetic.
#include "agentflow/provider_yaml_config.hpp"
#include "agentflow/provider_profile_runtime.hpp"
#include "agentflow/xlang_sqlite.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <barrier>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>

using namespace agentflow;
namespace {
using Json=nlohmann::json;
const std::string claude_key="synthetic-yaml-claude-key-not-live";
const std::string openai_key="synthetic-yaml-openai-key-not-live";
const std::vector<std::string> allowed={"anthropic.messages","openai.responses","openai.chat"};
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class E,class F>void rejects(F call){try{call();}catch(const E&){return;}throw std::runtime_error("Expected native YAML/profile rejection did not occur");}
void reject_yaml(const std::string& source,std::optional<ProviderYamlErrorCode> code={}){
    try{parse_provider_yaml_config(source,allowed);}
    catch(const ProviderYamlError& error){require(std::string(error.what())=="Provider YAML configuration rejected","No YAML exception/scalar/key/path may enter diagnostics");if(code)require(error.code()==*code,"Expected fixed YAML error classification");return;}
    throw std::runtime_error("Expected bounded native YAML rejection did not occur");
}
SecretBytes secret(const std::string& value){return SecretBytes({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
bool same(const SecretBytes& actual,const std::string& value){const auto bytes=actual.view();return bytes.size()==value.size()&&std::equal(bytes.begin(),bytes.end(),value.begin(),[](std::uint8_t a,char b){return a==static_cast<unsigned char>(b);});}
const SavedProviderProfile& profile(const ProviderProfileSnapshot& state,const std::string& id){const auto found=std::find_if(state.profiles.begin(),state.profiles.end(),[&](const auto& item){return item.id==id;});if(found==state.profiles.end())throw std::runtime_error("Expected actual saved profile missing");return *found;}
std::vector<ProviderProfileRoute> policy(const std::string& origin){return {
    {"anthropic.messages","anthropic",origin+"/messages","server","fixture:yaml:anthropic",ProviderWire::anthropic_messages},
    {"openai.responses","openai",origin+"/responses","server","fixture:yaml:responses",ProviderWire::responses},
    {"openai.chat","openai",origin+"/chat/completions","server","fixture:yaml:chat",ProviderWire::chat_completions}};}
std::vector<ProviderProfileExecutionPolicy> runtime_policy(const std::string& origin){std::vector<ProviderProfileExecutionPolicy> result;for(const auto& route:policy(origin)){ChatProviderConfig provider;provider.endpoint=route.endpoint;provider.wire=route.wire;provider.tools=Capability::supported;provider.stream_usage=Capability::supported;provider.output_limit=Capability::supported;provider.deadline=std::chrono::seconds(5);provider.idle_timeout=std::chrono::seconds(3);result.push_back({route,provider,route.provider=="anthropic"?std::optional<ProviderCataloguePolicy>{{origin+"/claude-models",ProviderCatalogueFormat::anthropic}}:std::nullopt});}return result;}
ProviderProfileSnapshot apply(ProviderProfiles& profiles,const std::string& source,std::int64_t expected,
    ProviderProfiles::Validator each={},ProviderProfiles::Validator active={}){auto config=parse_provider_yaml_config(source,allowed);return profiles.save_config(std::move(config.profiles),std::move(config.active_profile),expected,std::move(each),std::move(active));}
std::string document(){return "# Explicit synthetic native YAML fixture.\nversion: 1\nactive_profile: claude\nprofiles:\n  claude:\n    route: anthropic.messages\n    api_key: \""+claude_key+"\"\n  openai:\n    route: openai.responses\n    api_key: '"+openai_key+"'\n    model: synthetic-openai-model\n";}
void pure_parser(const std::filesystem::path& root){
    auto parsed=parse_provider_yaml_config(document(),allowed);
    require(parsed.profiles.size()==2&&parsed.active_profile=="claude"&&parsed.profiles[0].id=="claude"&&!parsed.profiles[0].model&&same(parsed.profiles[0].key,claude_key),"Genuine block YAML preserves a real key-only input without inventing a model");
    parsed=parse_provider_yaml_config("\xef\xbb\xbf"+document(),allowed);
    require(parsed.profiles.size()==2,"Native YAML reads valid UTF8 BOM and comments");
    parsed=parse_provider_yaml_config("version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: \"\"}}\n",allowed);
    require(parsed.profiles.size()==1&&parsed.profiles[0].key.view().empty()&&!parsed.profiles[0].model,"Empty-key template is syntactically valid but does not grant enrollment");
    parsed=parse_provider_yaml_config("version: 1\nprofiles: {\"clau\\u0064e\": {route: anthropic.messages, api_key: 'synthetic-flow-key'}}\n",allowed);
    require(parsed.profiles[0].id=="claude"&&same(parsed.profiles[0].key,"synthetic-flow-key"),"Native quoted/flow YAML decoding is real parser behavior");
    const auto synthetic_file=root/"synthetic-providers.yaml";
    {std::ofstream file(synthetic_file,std::ios::binary);file<<document();require(static_cast<bool>(file),"Synthetic fixture file write failed");}
    parsed=read_provider_yaml_config(synthetic_file,allowed);
    require(parsed.profiles.size()==2&&same(parsed.profiles[0].key,claude_key),"Native bounded file import reads only the owned synthetic file");
    rejects<ProviderYamlError>([&]{read_provider_yaml_config(std::filesystem::path("relative-fixture.yaml"),allowed);});
    rejects<ProviderYamlError>([&]{read_provider_yaml_config(root/"absent-fixture.yaml",allowed);});
    for(const auto& source:std::vector<std::string>{
        "version: 1\nversion: 1\nprofiles: {}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, route: anthropic.messages}}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages}, \"clau\\u0064e\": {route: anthropic.messages}}\n"})reject_yaml(source,ProviderYamlErrorCode::duplicate_field);
    for(const auto& source:std::vector<std::string>{
        "version: 1\nprofiles: &profiles {}\n", "version: 1\nprofiles: *missing\n",
        "version: 1\nprofiles: !private {}\n", "version: 1\nprofiles: []\n",
        "version: 1\nprofiles: {}\n---\nversion: 1\nprofiles: {}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: !!int 12345}}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: null}}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: \"private malformed key}}\n"})reject_yaml(source);
    for(const auto& source:std::vector<std::string>{
        "version: 2\nprofiles: {}\n", "version: '1'\nprofiles: {}\n", "profiles: {}\n",
        "version: 1\nprofiles: {}\nendpoint: https://unapproved.invalid\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, endpoint: https://unapproved.invalid}}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, models: [fake]}}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, model: \"\"}}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages}}\nactive_profile: {}\n",
        "version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: 'contains spaces'}}\n"})reject_yaml(source);
    reject_yaml("version: 1\nprofiles: {claude: {route: unapproved.route}}\n",ProviderYamlErrorCode::route_unavailable);
    reject_yaml(std::string("version: 1\nprofiles: {}\n#")+std::string("\xc0\xaf",2),ProviderYamlErrorCode::invalid_utf8);
    reject_yaml(std::string("version: 1\nprofiles: {}\n#")+std::string("\xed\xa0\x80",3),ProviderYamlErrorCode::invalid_utf8);
    reject_yaml(std::string("version: 1\nprofiles: {}\n#")+std::string(1,'\0'),ProviderYamlErrorCode::invalid_utf8);
    const std::string prefix="version: 1\nprofiles: {}\n#";
    parsed=parse_provider_yaml_config(prefix+std::string(256*1024-prefix.size(),'c'),allowed);require(parsed.profiles.empty(),"Exact bounded file-size YAML remains valid");
    reject_yaml(prefix+std::string(256*1024-prefix.size()+1,'c'),ProviderYamlErrorCode::exceeds_limits);
    parsed=parse_provider_yaml_config("version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: '"+std::string(32768,'A')+"'}}",allowed);require(parsed.profiles[0].key.view().size()==32768,"Exact native scalar/key boundary is supported");
    reject_yaml("version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: '"+std::string(32769,'A')+"'}}",ProviderYamlErrorCode::exceeds_limits);
    std::string profiles32="version: 1\nprofiles:\n";for(int i=0;i<32;++i)profiles32+="  p"+std::to_string(i)+": {route: anthropic.messages}\n";
    require(parse_provider_yaml_config(profiles32,allowed).profiles.size()==32,"Exact 32 native profile bound is supported");reject_yaml(profiles32+"  p32: {route: anthropic.messages}\n",ProviderYamlErrorCode::invalid_schema);
    std::string depth="version: 1\nprofiles: ";for(int i=0;i<12;++i)depth+="{nested: ";depth+="{}";for(int i=0;i<12;++i)depth+="}";reject_yaml(depth,ProviderYamlErrorCode::exceeds_limits);
    std::string many_nodes="version: 1\nprofiles: {}\n";for(int i=0;i<260;++i)many_nodes+="field"+std::to_string(i)+": value\n";reject_yaml(many_nodes,ProviderYamlErrorCode::exceeds_limits);
}
void registry(const std::filesystem::path& root,const std::vector<std::string>& imports,const std::string& origin){
    const auto database=(root/"provider-yaml.sqlite").string();ProviderProfileSnapshot saved;
    {
        PersistenceService store(database,imports);ProviderProfiles profiles(store,policy(origin));
        std::size_t each=0,prepared=0;
        saved=apply(profiles,document(),0,[&](const auto& value,const auto& route){++each;auto owned=store.resolve_credential(route.credential_scope,value.credential_id,route.credential_purpose).get();require(same(owned,value.id=="claude"?claude_key:openai_key),"Each native candidate already owns an encrypted key before validation");},[&](const auto& value,const auto&){++prepared;require(value.id=="claude"&&value.model.empty(),"Only final active key-only candidate is prepared");});
        require(saved.revision==1&&saved.active=="claude"&&saved.profiles.size()==2&&profile(saved,"claude").model.empty()&&each==2&&prepared==1,"Entire native document publishes once with an honest key-only active profile");
        const auto source=store.information("native-provider-profiles","registry").get();require(source.find(claude_key)==std::string::npos&&source.find(openai_key)==std::string::npos,"Registry metadata never contains plaintext YAML keys");
        const auto credential_count=store.credentials("server").get().size();
        require(credential_count==2&&same(profiles.credential("claude"),claude_key),"Key-only native profile is genuinely encrypted and independently resolvable");
        auto unchanged=apply(profiles,document(),1);
        require(unchanged.revision==1&&profile(unchanged,"claude").revision==1&&profile(unchanged,"claude").credential_id==profile(saved,"claude").credential_id&&store.credentials("server").get().size()==2,"Identical startup config cannot rotate credential/profile/registry revisions");
        unchanged=apply(profiles,"version: 1\nprofiles: {claude: {route: anthropic.messages}, openai: {route: openai.responses, api_key: \"\"}}\n",1);
        require(unchanged.revision==1&&profile(unchanged,"openai").model=="synthetic-openai-model"&&store.information("native-provider-profiles","registry").get()==source,"Missing model and empty/omitted key reuse exact existing choices/references");
        rejects<std::invalid_argument>([&]{profiles.save("public-key-only","anthropic.messages","",secret(claude_key),1);});
        rejects<std::invalid_argument>([&]{apply(profiles,"version: 1\nprofiles: {new: {route: anthropic.messages, api_key: \"\"}}",1);});
        rejects<NotFound>([&]{apply(profiles,"version: 1\nactive_profile: absent\nprofiles: {claude: {route: anthropic.messages, api_key: synthetic-unpublished-key}}",1);});
        require(store.credentials("server").get().size()==credential_count,"Unknown final active selection is checked before any candidate encryption");
        // Semantically valid YAML, bad SECOND native provider-family mutation.
        rejects<std::invalid_argument>([&]{apply(profiles,"version: 1\nprofiles: {new: {route: openai.responses, api_key: synthetic-new-key}, claude: {route: openai.chat, api_key: synthetic-changed-family-key}}",1);});
        require(store.information("native-provider-profiles","registry").get()==source&&store.credentials("server").get().size()==credential_count&&same(profiles.credential("claude"),claude_key),"Bad second profile cannot publish first candidate or write credentials before complete semantic validation");
        rejects<std::invalid_argument>([&]{apply(profiles,"version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: synthetic-new-claude}, new: {route: openai.responses, api_key: synthetic-new-openai}}",1,[&](const auto& value,const auto&){if(value.id=="new")throw std::invalid_argument("Synthetic second static validation refusal");});});
        require(store.information("native-provider-profiles","registry").get()==source&&same(profiles.credential("claude"),claude_key),"Second candidate validator failure leaves only unreferenced encrypted candidates; no active ref or registry publication");
        {XlangSqlite inject(database,imports);inject.execute("CREATE TRIGGER injected_yaml_registry BEFORE UPDATE ON information WHEN NEW.category='native-provider-profiles' BEGIN SELECT RAISE(ABORT,'synthetic YAML CAS publication fault'); END");}
        rejects<DatabaseError>([&]{apply(profiles,"version: 1\nactive_profile: openai\nprofiles: {claude: {route: anthropic.messages, api_key: synthetic-fault-key}}",1);});
        require(store.information("native-provider-profiles","registry").get()==source&&profiles.snapshot().active=="claude"&&same(profiles.credential("claude"),claude_key),"Actual SQLite publication fault preserves committed active credential and registry");
        {XlangSqlite inject(database,imports);inject.execute("DROP TRIGGER injected_yaml_registry");const auto rows=inject.execute("SELECT ciphertext FROM credentials WHERE id=?",{profile(saved,"claude").credential_id}).rows;require(rows.size()==1&&std::holds_alternative<SqlBytes>(rows[0][0]),"Real encrypted key-only storage must hold a ciphertext BLOB");const auto& blob=std::get<SqlBytes>(rows[0][0]);require(std::search(blob.begin(),blob.end(),claude_key.begin(),claude_key.end())==blob.end(),"Actual stored credential cannot be plaintext YAML bytes");}
        rejects<Conflict>([&]{apply(profiles,document(),0);});
        store.close();
    }
    {
        PersistenceService store(database,imports);ProviderProfiles profiles(store,policy(origin));
        const auto restored=profiles.snapshot();require(restored.revision==1&&profile(restored,"claude").model.empty()&&profile(restored,"claude").credential_id==profile(saved,"claude").credential_id&&same(profiles.credential("claude"),claude_key),"Restart preserves actual encrypted key-only profile/reference without rotation");
        AgentSettings base;ProviderProfileRuntime runtime(store,base,runtime_policy(origin),1,8);
        require(!runtime.available()&&runtime.models().empty()&&runtime.configuration().active=="claude","Key-only active runtime stays visibly unconfigured without a fictional model");
        auto discovered=runtime.discover_models("claude","anthropic.messages",secret(""),1);
        require(discovered==std::vector<std::string>{"claude-synthetic-a","claude-synthetic-b"},"Existing native catalogue can use the actual encrypted key-only profile");
        require(profile(profiles.snapshot(),"claude").model.empty(),"Discovery does not secretly select or invent a model");
        const auto selected=runtime.save_profile("claude","anthropic.messages",discovered[0],secret(""),1,true);
        require(selected.revision==2&&runtime.available()&&runtime.models()==std::vector<std::string>{discovered[0]},"Public native model selection can complete the genuinely stored key-only configuration");
        auto again=apply(profiles,document(),2);
        require(again.revision==2&&profile(again,"claude").model==discovered[0],"YAML without model preserves the later GUI/API-selected model instead of resetting it");
    }
    {
        PersistenceService store((root/"yaml-selection-only.sqlite").string(),imports);ProviderProfiles profiles(store,policy(origin));
        const auto first=apply(profiles,document(),0);const auto credential_count=store.credentials("server").get().size();std::size_t prepared=0;
        auto selected=profiles.save_config({},std::string("openai"),1,{},[&](const auto& value,const auto&){++prepared;require(value.id=="openai","Selection-only import prepares exactly its merged existing profile");});
        require(selected.revision==2&&selected.active=="openai"&&selected.profiles.size()==2&&prepared==1&&profile(selected,"claude").credential_id==profile(first,"claude").credential_id&&store.credentials("server").get().size()==credential_count,"Empty supplied batch plus explicit existing selection publishes once without key rotation");
        selected=profiles.save_config({},std::string("openai"),2);
        require(selected.revision==2&&store.credentials("server").get().size()==credential_count,"Identical selection-only document rechecks CAS without increment or encryption");
        rejects<NotFound>([&]{profiles.save_config({},std::string("absent"),2);});
        rejects<std::invalid_argument>([&]{profiles.save_config({},std::nullopt,2);});
        rejects<Conflict>([&]{profiles.save_config({},std::string("claude"),1);});
        require(profiles.snapshot().revision==2&&profiles.snapshot().active=="openai","Bad empty-batch selection cannot mutate actual merged registry");
    }
    {
        PersistenceService store((root/"yaml-runtime-import.sqlite").string(),imports);AgentSettings base;ProviderProfileRuntime runtime(store,base,runtime_policy(origin),1,8);
        const auto file=root/"synthetic-runtime-providers.yaml";auto write=[&](const std::string& raw){std::ofstream stream(file,std::ios::binary|std::ios::trunc);stream<<raw;require(static_cast<bool>(stream),"Owned synthetic runtime YAML write failed");};
        write("version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: \"\"}}\n");
        auto observed=runtime.import_yaml_configuration(file,0);
        require(observed.revision==0&&observed.active.empty()&&observed.profiles.empty()&&!runtime.available()&&store.credentials("server").get().empty(),"Real startup importer keeps a fresh empty template unconfigured without fake key/model/active selection");
        write(document());observed=runtime.import_yaml_configuration(file,0);
        require(observed.revision==1&&observed.active=="claude"&&observed.profiles.size()==2&&observed.profiles[0].model.empty()&&!runtime.available()&&runtime.models().empty(),"Real startup importer publishes encrypted key-only active profile without inferring a model");
        const auto registry_bytes=store.information("native-provider-profiles","registry").get();const auto credentials=store.credentials("server").get().size();
        observed=runtime.import_yaml_configuration(file,1);
        require(observed.revision==1&&store.information("native-provider-profiles","registry").get()==registry_bytes&&store.credentials("server").get().size()==credentials,"Real repeated startup import cannot rotate encrypted references/revisions");
        write("version: 1\nactive_profile: openai\nprofiles: {new_placeholder: {route: anthropic.messages, api_key: \"\"}}\n");
        observed=runtime.import_yaml_configuration(file,1);
        require(observed.revision==2&&observed.active=="openai"&&observed.profiles.size()==2&&runtime.available()&&runtime.models()==std::vector<std::string>{"synthetic-openai-model"}&&store.credentials("server").get().size()==credentials,"Filtered zero-entry document may select a real existing executable profile without enrollment or provider inference");
        const auto selected_bytes=store.information("native-provider-profiles","registry").get();
        write("version: 1\nprofiles: {new: {route: openai.responses, api_key: synthetic-unpublished-key}, claude: {route: openai.chat, api_key: synthetic-bad-family-key}}\n");
        rejects<std::invalid_argument>([&]{runtime.import_yaml_configuration(file,2);});
        require(runtime.configuration().revision==2&&runtime.configuration().active=="openai"&&runtime.available()&&store.information("native-provider-profiles","registry").get()==selected_bytes&&store.credentials("server").get().size()==credentials,"Bad second runtime profile preserves actual service/registry/key references before encryption/publication");
    }
    {
        PersistenceService store((root/"yaml-race.sqlite").string(),imports);ProviderProfiles left(store,policy(origin)),right(store,policy(origin));
        apply(left,"version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: '"+claude_key+"'}}",0);
        std::barrier admitted(2);
        auto writer=[&](ProviderProfiles& registry,const std::string& id){try{return apply(registry,"version: 1\nprofiles: {"+id+": {route: openai.responses, api_key: '"+openai_key+"'}}",1,[&](const auto&,const auto&){admitted.arrive_and_wait();}).revision==2;}catch(const Conflict&){return false;}};
        auto a=std::async(std::launch::async,[&]{return writer(left,"left");}),b=std::async(std::launch::async,[&]{return writer(right,"right");});
        require(static_cast<int>(a.get())+static_cast<int>(b.get())==1&&left.snapshot().revision==2&&left.snapshot().profiles.size()==2,"Two whole-document publishers have exactly one registry CAS winner");
    }
    {
        PersistenceService store((root/"yaml-max-revision.sqlite").string(),imports);ProviderProfiles profiles(store,policy(origin));
        store.put_credential("server","max-fixture-key","fixture:yaml:anthropic","Synthetic migration revision boundary",secret(claude_key),0).get();
        profiles.import_existing("claude","anthropic.messages","","max-fixture-key",9007199254740991);
        const auto unchanged=apply(profiles,"version: 1\nprofiles: {claude: {route: anthropic.messages, api_key: '"+claude_key+"'}}",9007199254740991);
        require(unchanged.revision==9007199254740991&&profile(unchanged,"claude").credential_id=="max-fixture-key","Exact maximum revision still permits idempotent config CAS without increment/rotation");
        rejects<Conflict>([&]{apply(profiles,"version: 1\nprofiles: {new: {route: openai.responses, api_key: '"+openai_key+"'}}",9007199254740991);});
        require(store.credentials("server").get().size()==1,"Exhausted changed document cannot encrypt candidates or publish overflow");
    }
}
}
int main(int argc,char** argv){if(argc!=5)return 2;try{const std::filesystem::path root=argv[1];const std::vector<std::string> imports={argv[2],argv[3]};pure_parser(root);registry(root,imports,argv[4]);std::cout<<"Native provider YAML contract passed genuine bounded YAML syntax, value-free rejects, atomic full-document candidate/CAS publication, encrypted key-only profile/restart, unchanged configuration idempotency and existing native catalogue/model-selection integration. YAML/key/catalogue replies are synthetic; no provider inference/live startup file was tested.\n";return 0;}catch(const std::exception& error){try{std::ofstream private_log(std::filesystem::u8path(argv[1])/"native-yaml-fixture-failure-private.log",std::ios::binary);private_log<<error.what();}catch(...){}std::cerr<<"Native synthetic provider YAML contract failed; private diagnostics retained, no source/key/parser exception is printed.\n";return 1;}}
