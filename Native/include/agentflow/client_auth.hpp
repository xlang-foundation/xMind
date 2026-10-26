#pragma once
#include "agentflow/secret_protection.hpp"
#include <optional>
#include <string>
#include <vector>
namespace agentflow {
// Trusted local host credential cache, independent of workspace execution.
// C++ binds DPAPI to the private directory and canonical loopback origin;
// encrypted blobs alone cross the embedded xlang3 SQLite boundary.
void save_client_auth(const std::string& directory,const std::vector<std::string>& roots,
                      const std::string& origin,const SecretBytes& token,bool replace=true);
std::optional<SecretBytes> load_client_auth(const std::string& directory,
    const std::vector<std::string>& roots,const std::string& origin);
void delete_client_auth(const std::string& directory,const std::vector<std::string>& roots,
                        const std::string& origin);
// Private stdin/stdout bridge; no secret is accepted in argv.
int run_client_auth(int argc,char** argv);
}
