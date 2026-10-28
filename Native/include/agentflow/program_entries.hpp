#pragma once
#include <string>
#include <functional>
#include <memory>
#include "agentflow/local_profile.hpp"
// Native entry handlers shared by the unified product and transitional launchers.
// These do not transfer backend ownership to a client.
int run_server(int argc,char** argv);
int cli_main(int argc,char** argv,const std::string& workspace={},
    const std::function<agentflow::LocalProfileConnection()>& profile={});
int run_schema_worker(int argc,char** argv);
#if defined(_WIN32)
int run_admin(int argc,char** argv);
namespace agentflow {int run_xmind_ipc_client(int argc,char** argv);}
#endif

#if defined(_WIN32)
int run_local_view(int argc, char** argv);
int run_web(int argc,char** argv);
int run_embedded_local_web_view(std::shared_ptr<agentflow::LocalProfileConnection>,const std::string& assets,const std::string& packages,const std::string& ready,int port=0,std::function<bool()> running={});
namespace agentflow {
LocalProfileOptions managed_web_view_options(const std::string& profile_directory);
void publish_managed_web_port(const std::string& profile_directory,int port);
std::string inspect_managed_web_owner(const std::string& profile_directory);
std::string finish_retired_managed_web_owner(const std::string& profile_directory);
}
#endif
