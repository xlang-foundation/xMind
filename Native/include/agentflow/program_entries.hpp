#pragma once
#include <string>
#include <functional>
#include "agentflow/local_profile.hpp"
// Native entry handlers shared by the unified product and transitional launchers.
// These do not transfer backend ownership to a client.
int run_server(int argc,char** argv);
int cli_main(int argc,char** argv,const std::string& workspace={},
    const std::function<agentflow::LocalProfileConnection()>& profile={});
int run_schema_worker(int argc,char** argv);
#if defined(_WIN32)
int run_admin(int argc,char** argv);
#endif

#if defined(_WIN32)
int run_local_view(int argc, char** argv);
#endif
