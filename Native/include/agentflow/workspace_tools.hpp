#pragma once
#include "agentflow/model_provider.hpp"
#include <memory>
#include <optional>

namespace agentflow {
struct ToolAccessDenied : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolFileError : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolCancelled : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolContentConflict : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolMutationUncertain : std::runtime_error {using std::runtime_error::runtime_error;};
struct WorkspaceFile {std::string path,content;};
struct WorkspaceSnapshot {
    std::string path,content,workspace_id,file_id,content_sha256;
};
struct WorkspaceEditPlan {
    WorkspaceSnapshot before;
    std::string after_content,after_sha256;
    std::size_t replaced_occurrences;
};
struct WorkspaceCreatePlan {std::string path,workspace_id,parent_id,content,content_sha256;};
struct WorkspaceFingerprint {std::string path,workspace_id,file_id,content_sha256;std::size_t size;};
struct WorkspaceEntry {std::string name,kind;};
struct WorkspaceListing {std::vector<WorkspaceEntry> entries;bool truncated=false;};
struct WorkspaceMatch {std::string path,text;std::int64_t line;bool text_truncated=false;};
struct WorkspaceSearch {
    std::vector<WorkspaceMatch> matches;
    std::size_t scanned_files=0,skipped_entries=0;
    bool truncated=false;
};
// Workspace boundary is an opened OS directory, not a caller-controlled path.
// Model definitions remain read-only; backend mutations require separate policy contracts.
class WorkspaceTools {
public:
    explicit WorkspaceTools(const std::string& root);
    ~WorkspaceTools();
    WorkspaceTools(const WorkspaceTools&)=delete;
    WorkspaceTools& operator=(const WorkspaceTools&)=delete;
    // Local volume/file identity from the opened directory handle, never a
    // view's path spelling. Shared servers must namespace it by trusted worker
    // identity. Changed root path requires reopening the runtime.
    std::string identity() const;
    WorkspaceFile read_file(const std::string& path,std::stop_token cancel={}) const;
    // Backend edit preconditions captured from one verified file handle.
    // Capturing a snapshot does not grant permission or mutate the file.
    WorkspaceSnapshot snapshot_file(const std::string& path,std::stop_token cancel={}) const;
    // Guidance reads retain verified directory handles, reject links, and
    // distinguish an absent final file from an unreadable/unsafe file.
    std::optional<WorkspaceSnapshot> instruction_file(const std::string& path,std::stop_token cancel={}) const;
    // Root-to-directory AGENTS.md snapshots; no home/global or sibling reads.
    std::vector<WorkspaceSnapshot> repository_instructions(const std::string& directory=".",std::stop_token cancel={}) const;
    // Build a reviewable literal replacement from actual file bytes. This
    // returns a proposal only; applying it requires a claimed effect and an
    // executor that revalidates its exact snapshot preconditions.
    WorkspaceEditPlan plan_replacement(const std::string& path,const std::string& old_text,
        const std::string& new_text,std::size_t expected_occurrences=1,std::stop_token cancel={}) const;
    // Backend effect primitive, not a model-invokable tool. The caller must
    // hold a matching durable operation claim and record the actual outcome.
    // In-place application is not atomic replacement. After writes begin, any
    // failure/cancellation is uncertain and must not be retried blindly.
    WorkspaceSnapshot apply_plan(const WorkspaceEditPlan& plan,std::stop_token cancel={}) const;
    // Existing parent, absent final entry. Captures identity/bytes only; no file
    // is created until the owning executor has a matching durable claim.
    WorkspaceCreatePlan plan_creation(const std::string& path,const std::string& content,std::stop_token cancel={}) const;
    // Handle-relative create-new never overwrites an existing entry. Creation
    // itself is an effect, including an empty file; subsequent failures are uncertain.
    WorkspaceSnapshot apply_creation(const WorkspaceCreatePlan& plan,std::stop_token cancel={}) const;
    // Reconciliation inspection hashes bounded raw bytes, including a partial
    // write that is no longer valid UTF-8. It never exposes those bytes to models.
    WorkspaceFingerprint fingerprint_file(const std::string& path,std::stop_token cancel={}) const;
    WorkspaceListing list_files(const std::string& path=".",std::stop_token cancel={}) const;
    WorkspaceSearch search_files(const std::string& query,std::stop_token cancel={}) const;
    static std::vector<ModelToolDefinition> definitions();
    // Validate exact built-in argument shapes and return a bounded JSON result.
    std::string invoke(const std::string& name,const std::string& arguments_json,std::stop_token cancel={}) const;
private:
    WorkspaceSnapshot read_snapshot(const std::string& path,bool capture_version,std::stop_token cancel,bool require_text=true) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
