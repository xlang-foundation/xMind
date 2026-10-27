#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <vector>

namespace agentflow {
struct WorkspaceNameConflict : std::runtime_error {using std::runtime_error::runtime_error;};
struct WorkspaceFolderRecord {
    std::string folder_id, identity, root, name;
    std::int64_t ordinal=0;
};
struct WorkspaceGroupRecord {
    std::string workspace_id, name, primary_folder_id;
    std::int64_t revision=0;
    std::vector<WorkspaceFolderRecord> folders;
};

// Local workspace membership is metadata only. Every folder is re-opened by
// the execution authority before use; this registry never grants file access.
// SQLite calls use the embedded xlang3 binding and each method owns its runtime
// on the calling thread.
// Private storage must already exist. Its native directory handle supplies the
// canonical overlap boundary; aliases do not change workspace eligibility.
class WorkspaceRegistry {
public:
    WorkspaceRegistry(std::string database,std::vector<std::string> xlang_import_roots,
        std::string private_storage_root);
    WorkspaceGroupRecord create(const std::string& primary_root,const std::string& name={}) const;
    WorkspaceGroupRecord add_folder(const std::string& workspace_id,const std::string& root) const;
    WorkspaceGroupRecord rename(const std::string& workspace_id,const std::string& name,const std::string& expected_name) const;
    std::vector<WorkspaceGroupRecord> list() const;
    WorkspaceGroupRecord get(const std::string& workspace_id) const;
private:
    std::string database_,private_storage_root_;
    std::vector<std::string> xlang_import_roots_;
};
}
