#include "agentflow/skill_context.hpp"
#include "agentflow/repository_instruction_context.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace agentflow;
using Json=nlohmann::json;
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Error,class Action>void rejects(Action action){try{action();}catch(const Error&){return;}throw std::runtime_error("Expected native skill rejection did not occur");}
void write(const std::filesystem::path& file,const std::string& text){std::ofstream out(file,std::ios::binary|std::ios::trunc);out.write(text.data(),text.size());out.close();need(bool(out),"Fixture write failed");}
std::string guide(const std::string& body){return "---\nname: inspect\ndescription: Synthetic native skill fixture\nmetadata:\n  fixture: explicitly synthetic\n---\n"+body;}
}
int main(int argc,char** argv){if(argc!=2&&argc!=3)return 2;try{
    const auto root=std::filesystem::u8path(argv[1]);WorkspaceTools workspace(root.string());SkillContext skills(workspace);
    if(argc==3){need(std::string(argv[2])=="--catalogue","Unknown fixture mode");std::cout<<skills.catalogue_json()<<'\n';return 0;}
    need(Json::parse(skills.catalogue_json())["skills"].empty()&&skills.prepare().empty(),"Absent skill roots must not manufacture guidance");
    const auto directory=root/".agents"/"skills"/"inspect",file=directory/"SKILL.md";std::filesystem::create_directories(directory);
    write(file,guide("Synthetic active instructions: inspect only actual returned bytes.\n"));
    const auto catalogue=Json::parse(skills.catalogue_json());need(catalogue["skills"].size()==1&&catalogue["skills"][0]["id"]=="inspect", "Native discovery must use current verified workspace files");
    const auto advertised=skills.prepare();need(advertised.find("Synthetic native skill fixture")!=std::string::npos&&advertised.find("Synthetic active instructions")==std::string::npos,"Discovery must advertise metadata without activating body instructions");
    rejects<std::invalid_argument>([&]{skills.activate(R"({"id":"inspect","path":"../outside"})");});rejects<std::invalid_argument>([&]{skills.activate(R"({"id":"inspect","id":"other"})");});rejects<ToolAccessDenied>([&]{skills.activate(R"({"id":"missing"})");});
    need(skills.activation_directory(R"({"id":"inspect"})")==".agents/skills/inspect","Activation must derive its directory from Native discovery");
    const auto requested=Json::parse(skills.activate(R"({"id":"inspect"})"));need(requested["effect_permission"]==false&&!skills.ready(),"Loading must not grant effect authority or deliver guidance within the same model batch");
    need(skills.prepare().find("Synthetic active instructions")!=std::string::npos&&skills.ready(),"Next native provider context must deliver the current active body");
    const auto condition=skills.precondition();condition.verify({});need(condition.metadata_json.find("Synthetic active instructions")==std::string::npos&&Json::parse(condition.metadata_json)[0]["content_sha256"].get<std::string>().size()==64,"Approval must retain source identity/hash metadata, never body text");
    const auto before_rejected_load=skills.metadata();rejects<ToolFileError>([&]{skills.activate(R"({"id":"inspect"})",{},1);});need(skills.ready()&&skills.metadata()==before_rejected_load,"A rejected load must preserve already delivered guidance and readiness");
    {
        const auto skill_root=directory.parent_path();for(const auto* name:{"big-one","big-two","big-three"}){const auto path=skill_root/name;need(std::filesystem::create_directory(path),"Budget fixtures must be fresh directories");write(path/"SKILL.md",std::string("---\nname: ")+name+"\ndescription: Synthetic aggregate budget fixture\n---\n"+std::string(12000,'x'));}
        SkillContext bounded(workspace);bounded.prepare();bounded.activate(R"({"id":"big-one"})");bounded.prepare();bounded.activate(R"({"id":"big-two"})");bounded.prepare();const auto retained=bounded.metadata();rejects<ToolFileError>([&]{bounded.activate(R"({"id":"big-three"})");});need(bounded.ready()&&bounded.metadata()==retained,"Aggregate overflow must be rejected before activating a third guide or retiring existing guidance");
        for(const auto* name:{"big-one","big-two","big-three"}){const auto path=std::filesystem::weakly_canonical(skill_root/name);need(path.parent_path()==std::filesystem::weakly_canonical(skill_root)&&path.filename()==name,"Unsafe budget fixture cleanup path");std::filesystem::remove_all(path);}
    }
    write(file,guide("Synthetic changed instructions.\n"));need(!skills.ready(),"Changed source bytes must retire delivered guidance");rejects<ToolGuidanceChanged>([&]{condition.verify({});});need(skills.prepare().find("Synthetic changed instructions")!=std::string::npos&&skills.ready(),"Refresh must replace the old active body");
    RepositoryInstructionContext repository(workspace,workspace.repository_instructions());repository.prepare();repository.skills().activate(R"({"id":"inspect"})");need(!repository.ready("."),"Repository dispatch must defer pending skill delivery");repository.prepare();need(repository.ready("."),"Delivered skill context permits ordinary repository readiness");const auto bound=repository.precondition(".");need(Json::parse(bound.metadata_json)["skills"].size()==1,"Native effects must bind active skill snapshots into durable approval metadata");
    rejects<ToolFileError>([&]{repository.activate_skill(R"({"id":"inspect"})",65536);});need(repository.ready("."),"Combined instruction overflow must leave the delivered repository/skill context usable");
    write(file,guide("Synthetic newer instructions.\n"));rejects<ToolGuidanceChanged>([&]{bound.verify({});});repository.prepare();
    std::stop_source stopped;stopped.request_stop();rejects<ToolCancelled>([&]{skills.catalogue_json(stopped.get_token());});
    for(const auto& text:{std::string("name: inspect\ndescription: missing delimiter"),std::string("---\nname: inspect\nname: inspect\ndescription: duplicate\n---\nbody"),std::string("---\nname: wrong\ndescription: wrong identity\n---\nbody"),std::string("---\nname: inspect\ndescription: &hidden bound\nlicense: *hidden\n---\nbody"),std::string("---\nname: inspect\ndescription: [nested, sequence]\n---\nbody"),std::string("---\nname: inspect\ndescription: okay\nautoinvoke: perhaps\n---\nbody"),std::string("---\nname: inspect\ndescription: okay\n---\n  \n")}){write(file,text);rejects<ToolFileError>([&]{skills.catalogue_json();});}
    write(file,"---\nname: inspect\ndescription: Manual-only fixture\nautoinvoke: false\n---\nSynthetic manual-only instructions.\n");SkillContext manual(workspace);need(Json::parse(manual.catalogue_json())["skills"][0]["model_invocable"]==false&&manual.prepare().empty(),"Manual-only metadata must not be advertised as invocable guidance");rejects<ToolAccessDenied>([&]{manual.activate(R"({"id":"inspect"})");});
    write(file,guide("Restored fixture\n"));std::filesystem::remove(file);rejects<ToolGuidanceChanged>([&]{repository.prepare();});
    need(Json::parse(SkillContext(workspace).catalogue_json())["skills"].empty(),"Missing final SKILL.md is absent, not invented cached guidance");
    std::cout<<"Native workspace skills passed: verified discovery, bounded frontmatter, delivery barriers, source refresh and approval binding; no live model or script executed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
