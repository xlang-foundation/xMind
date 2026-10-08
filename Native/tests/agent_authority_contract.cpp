#include "agentflow/agent_authority.hpp"
#include "agentflow/agent_runner.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace agentflow;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action>void rejected(Action action){try{action();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Invalid authority binding was accepted");}
template<class Action>void metadata_rejected(Action action){try{action();}catch(const Conflict&){return;}throw std::runtime_error("Invalid credential metadata selection was accepted");}
AgentSettings settings(){
    AgentSettings value;value.workspace="D:/synthetic/workspace";
    value.provider={"https://provider.example.test/v1/responses","synthetic-model"};
    value.provider.wire=ProviderWire::responses;value.provider.tools=Capability::supported;
    value.credential=CredentialReference{"synthetic","provider","provider:synthetic"};
    value.provider_identity=ProviderExecutionIdentity{"synthetic-profile","synthetic-route","synthetic-provider",3};
    value.instructions="Synthetic backend instructions";
    value.instruction_policy.instructions="Synthetic policy with no persisted revision";
    value.approved_edits=true;value.delegation=AgentDelegationPolicy{};
    value.process_profiles.push_back({"synthetic-process","C:/synthetic/tool.exe",1,{"verify"},std::chrono::milliseconds(5000),"synthetic-executable-object"});
    value.mcp_servers.push_back({"synthetic-peer","C:/synthetic/peer.exe","D:/synthetic/workspace",1,true,{"--synthetic"},{{"PEER_KEY","synthetic","peer"}}});
    return value;
}
std::vector<AgentAuthorityCredentialVersion> versions(const AgentSettings& value){
    return {{value.credential->scope,value.credential->id,value.credential->purpose,3},
        {"synthetic","peer",mcp_credential_purpose(value.mcp_servers.front(),value.mcp_servers.front().credentials.front().name),7}};
}
std::string identity(const AgentSettings& value,const std::vector<AgentAuthorityCredentialVersion>& credentials){
    return agent_authority_identity(value,value.provider.model,"synthetic-workspace-object",credentials);
}
}
int main(){try{
    const auto base=settings();const auto metadata=versions(base);const auto bound=identity(base,metadata);
    require(bound.size()==64&&bound.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid private authority digest");
    require(agent_authority_identity(base,"","synthetic-workspace-object",metadata)==bound,"Default model admission changed authority");
    auto alternative=base;alternative.selectable_models.push_back("synthetic-alternate");
    require(agent_authority_identity(alternative,"synthetic-alternate","synthetic-workspace-object",metadata)!=identity(alternative,metadata),"Selected alternate model was not bound");
    auto reordered=metadata;std::reverse(reordered.begin(),reordered.end());require(identity(base,reordered)==bound,"Metadata order changed authority");
    auto different=base;different.instructions+=" changed";require(identity(different,metadata)!=bound,"Instructions were not bound");
    different=base;different.instruction_policy.instructions+=" changed";require(identity(different,metadata)!=bound,"Revision-zero policy content was not bound");
    different=base;different.instruction_policy.revision=1;require(identity(different,metadata)!=bound,"Instruction revision was not bound");
    different=base;different.provider.endpoint="https://other.example.test/v1/responses";require(identity(different,metadata)!=bound,"Private destination was not bound");
    different=base;different.provider.wire=ProviderWire::anthropic_messages;require(identity(different,metadata)!=bound,"Provider wire was not bound");
    different=base;different.provider_identity->profile_revision=4;require(identity(different,metadata)!=bound,"Admitted provider generation was not bound");
    different=base;different.provider.deadline+=std::chrono::milliseconds(1);require(identity(different,metadata)!=bound,"Transport policy was not bound");
    different=base;different.approved_edits=false;require(identity(different,metadata)!=bound,"Effect capability was not bound");
    different=base;different.process_profiles.front().prefix_arguments.push_back("changed");require(identity(different,metadata)!=bound,"Trusted process command was not bound");
    different=base;different.process_profiles.front().executable_id="other-executable-object";require(identity(different,metadata)!=bound,"Executable identity was not bound");
    different=base;different.mcp_servers.front().arguments.push_back("changed");require(identity(different,versions(different))!=bound,"MCP command/purpose was not bound");
    different=base;different.mcp_servers.front().revision=2;require(identity(different,versions(different))!=bound,"MCP generation was not bound");
    different=base;different.mcp_servers.front().credentials.front().name="ALTERNATE_KEY";require(identity(different,versions(different))!=bound,"MCP environment destination was not bound");
    different=base;different.credential->purpose="provider:other";require(identity(different,versions(different))!=bound,"Provider credential purpose was not bound");
    different=base;different.delegation->max_model_calls=16;require(identity(different,metadata)!=bound,"Shared budget policy was not bound");
    different=base;different.max_turns=8;require(identity(different,metadata)!=bound,"Parent turn quota was not bound");
    different=base;different.planning=DynamicPlanningPolicy{};const auto planning=identity(different,metadata);
    different.planning->human_expiry_ms--;require(identity(different,metadata)!=planning,"Human pause policy was not bound");
    different=base;different.planning=DynamicPlanningPolicy{};different.planning->max_nodes--;require(identity(different,metadata)!=planning,"Plan label policy was not bound");
    auto rotated=metadata;rotated.front().revision++;require(identity(base,rotated)!=bound,"Credential version was not bound");
    require(agent_authority_identity(base,base.provider.model,"other-workspace-object",metadata)!=bound,"Workspace object was not bound");
    auto invalid=metadata;invalid.pop_back();rejected([&]{identity(base,invalid);});
    invalid=metadata;invalid.push_back(metadata.front());rejected([&]{identity(base,invalid);});
    invalid=metadata;invalid.push_back({"synthetic","unused","provider:unused",1});rejected([&]{identity(base,invalid);});
    invalid=metadata;invalid.front().purpose="provider:other";rejected([&]{identity(base,invalid);});
    invalid=metadata;invalid.front().revision=0;rejected([&]{identity(base,invalid);});
    invalid=metadata;invalid.front().revision=9007199254740992;rejected([&]{identity(base,invalid);});
    rejected([&]{agent_authority_identity(base,"unconfigured-model","synthetic-workspace-object",metadata);});
    different=base;different.workspace.reset();rejected([&]{identity(different,metadata);});
    different=base;different.provider.tools=Capability::unknown;rejected([&]{identity(different,metadata);});
    different=base;different.process_profiles.front().executable_id.clear();rejected([&]{identity(different,metadata);});
    different=base;different.instruction_policy.instructions=std::string(1,static_cast<char>(0xff));rejected([&]{identity(different,metadata);});
    different=base;different.mcp_servers.front().arguments.push_back(std::string(1,static_cast<char>(0xff)));rejected([&]{identity(different,metadata);});
    different=base;different.mcp_servers.front().credentials.front().name=std::string(1,static_cast<char>(0xff));rejected([&]{identity(different,metadata);});
    different=base;different.process_profiles.front().prefix_arguments.resize(33);rejected([&]{identity(different,metadata);});
    different=base;different.selectable_models.resize(65,"synthetic-model");rejected([&]{identity(different,metadata);});
    std::vector<CredentialMetadata> rows;for(const auto& value:metadata)rows.push_back({value.scope,value.id,value.purpose,"Synthetic metadata label",value.revision});
    rows.push_back({"other-scope","provider","unrelated-purpose","Unrelated metadata",999});
    rows.push_back({"synthetic","unrelated","unrelated-purpose","Unrelated metadata",1});
    const auto selected=select_agent_authority_credentials(base,rows);
    require(selected.size()==2&&identity(base,selected)==bound,"Exact metadata selector used the wrong scope, ID or purpose");
    std::reverse(rows.begin(),rows.end());require(identity(base,select_agent_authority_credentials(base,rows))==bound,"Metadata row order changed authority");
    auto badrows=rows;badrows.push_back({"synthetic","provider","provider:synthetic","Duplicate synthetic metadata",3});metadata_rejected([&]{select_agent_authority_credentials(base,badrows);});
    badrows=rows;for(auto& value:badrows)if(value.scope=="synthetic"&&value.id=="provider")value.purpose="provider:wrong";metadata_rejected([&]{select_agent_authority_credentials(base,badrows);});
    badrows=rows;for(auto& value:badrows)if(value.scope=="synthetic"&&value.id=="provider")value.revision=0;metadata_rejected([&]{select_agent_authority_credentials(base,badrows);});
    badrows=rows;std::erase_if(badrows,[](const auto& value){return value.scope=="synthetic"&&value.id=="peer";});metadata_rejected([&]{select_agent_authority_credentials(base,badrows);});
    auto disabled=base;disabled.mcp_servers.front().enabled=false;
    const auto disabled_versions=select_agent_authority_credentials(disabled,badrows);
    require(disabled_versions.size()==1&&identity(disabled,disabled_versions)!=bound,"Disabled MCP incorrectly required peer credential metadata");
    std::cout<<"Native private agent authority bindings passed (synthetic settings and credential metadata; no key resolution, peer or provider execution).\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
