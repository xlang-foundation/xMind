#pragma once
#include "agentflow/model_provider.hpp"
#include <memory>

namespace agentflow {
struct ToolAccessDenied : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolFileError : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolCancelled : std::runtime_error {using std::runtime_error::runtime_error;};
struct WorkspaceFile {std::string path,content;};
struct WorkspaceEntry {std::string name,kind;};
struct WorkspaceListing {std::vector<WorkspaceEntry> entries;bool truncated=false;};
struct WorkspaceMatch {std::string path,text;std::int64_t line;bool text_truncated=false;};
struct WorkspaceSearch {
    std::vector<WorkspaceMatch> matches;
    std::size_t scanned_files=0,skipped_entries=0;
    bool truncated=false;
};
// Workspace boundary is an opened OS directory, not a caller-controlled path.
// Read-only tools; mutations/process execution require separate policy contracts.
class WorkspaceTools {
public:
    explicit WorkspaceTools(const std::string& root);
    ~WorkspaceTools();
    WorkspaceTools(const WorkspaceTools&)=delete;
    WorkspaceTools& operator=(const WorkspaceTools&)=delete;
    WorkspaceFile read_file(const std::string& path,std::stop_token cancel={}) const;
    WorkspaceListing list_files(const std::string& path=".",std::stop_token cancel={}) const;
    WorkspaceSearch search_files(const std::string& query,std::stop_token cancel={}) const;
    static std::vector<ModelToolDefinition> definitions();
    // Validate exact built-in argument shapes and return a bounded JSON result.
    std::string invoke(const std::string& name,const std::string& arguments_json,std::stop_token cancel={}) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
