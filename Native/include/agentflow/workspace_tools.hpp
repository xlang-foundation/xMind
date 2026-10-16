#pragma once
#include "agentflow/model_provider.hpp"
#include "agentflow/file_patch.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <functional>
#include <variant>

namespace agentflow {
struct ToolAccessDenied : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolFileError : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolCancelled : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolContentConflict : std::runtime_error {using std::runtime_error::runtime_error;};
struct ToolGuidanceChanged : ToolContentConflict {using ToolContentConflict::ToolContentConflict;};
struct ToolMutationUncertain : std::runtime_error {using std::runtime_error::runtime_error;};
struct WorkspaceFile {std::string path,content;};
struct WorkspaceFilePage {
    std::string path,content;
    std::size_t offset=1,lines_read=0;
    std::optional<std::size_t> next_offset;
    std::vector<std::size_t> truncated_lines;
};
struct WorkspaceDirectoryBinding {std::string path,file_id;};
struct WorkspaceSnapshot {
    std::string path,content,workspace_id,file_id,content_sha256;
    std::vector<WorkspaceDirectoryBinding> created_directory_bindings;
};
struct WorkspaceEditPlan {
    WorkspaceSnapshot before;
    std::string after_content,after_sha256;
    std::size_t replaced_occurrences;
    std::string parent_id;
};
struct WorkspaceCreatePlan {std::string path,workspace_id,parent_id,content,content_sha256;std::vector<std::string> create_directories;};
struct WorkspaceRemovalPlan {WorkspaceSnapshot before;std::string parent_id;};
struct WorkspaceMovePlan {WorkspaceSnapshot before;std::string parent_id;WorkspaceCreatePlan destination;};
struct WorkspaceRemovalResult {std::string path,workspace_id,file_id,removed_sha256;std::size_t removed_size;};
using WorkspacePatchFilePlan=std::variant<WorkspaceCreatePlan,WorkspaceEditPlan,WorkspaceRemovalPlan,WorkspaceMovePlan>;
struct WorkspacePatchPlan {std::string workspace_id;std::vector<WorkspacePatchFilePlan> files;};
struct WorkspaceFingerprint {std::string path,workspace_id,file_id,content_sha256;std::size_t size;};
struct WorkspacePathFingerprint {std::string path,workspace_id;std::optional<WorkspaceFingerprint> file;};
struct WorkspaceEntry {std::string name,kind;};
struct WorkspaceListing {std::vector<WorkspaceEntry> entries;bool truncated=false;};
struct WorkspaceMatch {std::string path,text;std::int64_t line;bool text_truncated=false;};
struct WorkspaceSearch {
    std::vector<WorkspaceMatch> matches;
    std::size_t scanned_files=0,skipped_entries=0;
    std::size_t ignored_entries=0,ignore_files=0;
    bool truncated=false;
    std::vector<std::string> limits;
};
struct WorkspaceSearchOptions {
    std::string path=".",include;
    bool regex=false,case_sensitive=true,hidden=false,respect_ignore=true;
    std::size_t limit=100;
};
struct WorkspaceGlob {
    std::vector<std::string> paths,limits;
    std::size_t scanned_entries=0,scanned_directories=0,skipped_entries=0,ignored_entries=0,ignore_files=0;
    bool truncated=false;
};
struct WorkspaceFindEntry {std::string path,type;};
struct WorkspaceFind {
    std::vector<WorkspaceFindEntry> entries;
    std::vector<std::string> limits;
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
    static std::string hash_content(std::string_view content);
    // Canonical public spelling of the same opened root, never the caller's
    // startup spelling. Revalidates its handle identity before observation.
    std::string root_path() const;
    // Backend-private state and generated dependency/cache directories are
    // outside model-visible workspace authority. Component matching includes
    // Windows case and trailing-dot/space aliases.
    static bool backend_private_component(std::string_view name);
    static bool same_relative_path(const std::string& first,const std::string& second);
    // Verified public directory identity for native process workdir admission;
    // validates both caller spelling and the normalized opened handle path.
    std::string directory_identity(const std::string& path=".",std::stop_token cancel={}) const;
    WorkspaceFile read_file(const std::string& path,std::stop_token cancel={}) const;
    // Public editor browsing uses the same verified root handles while
    // excluding Git metadata in addition to backend-private directories.
    WorkspaceListing browse_files(const std::string& path=".",std::stop_token cancel={}) const;
    WorkspaceFile browse_file(const std::string& path,std::stop_token cancel={}) const;
    // A bounded UTF-8 line page, not an edit snapshot. Offsets are 1-based.
    // Preserves source bytes/line endings except explicitly clipped lines.
    WorkspaceFilePage read_file_page(const std::string& path,std::size_t offset=1,
        std::size_t limit=2000,std::stop_token cancel={}) const;
    // Backend edit preconditions captured from one verified file handle.
    // Capturing a snapshot does not grant permission or mutate the file.
    WorkspaceSnapshot snapshot_file(const std::string& path,std::stop_token cancel={}) const;
    // Guidance reads retain verified directory handles, reject links, and
    // distinguish an absent final file from an unreadable/unsafe file.
    std::optional<WorkspaceSnapshot> instruction_file(const std::string& path,std::stop_token cancel={}) const;
    // Optional guidance/skill directory, traversed using retained parent
    // handles. Absence is distinct from links, unreadability and wrong types.
    std::optional<std::string> instruction_directory_identity(const std::string& path,std::stop_token cancel={}) const;
    // Root-to-directory AGENTS.md snapshots; no home/global or sibling reads.
    std::vector<WorkspaceSnapshot> repository_instructions(const std::string& directory=".",std::stop_token cancel={}) const;
    // Build a reviewable literal replacement from actual file bytes. This
    // returns a proposal only; applying it requires a claimed effect and an
    // executor that revalidates its exact snapshot preconditions.
    WorkspaceEditPlan plan_replacement(const std::string& path,const std::string& old_text,
        const std::string& new_text,std::size_t expected_occurrences=1,std::stop_token cancel={}) const;
    // All file sections are prepared from actual snapshots before any proposal
    // or effect. Delete/move plans are data, not callable mutation primitives.
    WorkspacePatchPlan plan_patch(const std::vector<FilePatch>& files,std::stop_token cancel={},bool create_parents=false) const;
    // Backend-only effects. Require a matching durable claim from an executor;
    // failure after any disposition/write/directory/rename attempt is uncertain.
    WorkspaceRemovalResult apply_removal(const WorkspaceRemovalPlan& plan,std::stop_token cancel={}) const;
    WorkspaceSnapshot apply_move(const WorkspaceMovePlan& plan,std::stop_token cancel={}) const;
    // Backend effect primitive, not a model-invokable tool. The caller must
    // hold a matching durable operation claim and record the actual outcome.
    // In-place application is not atomic replacement. After writes begin, any
    // failure/cancellation is uncertain and must not be retried blindly.
    WorkspaceSnapshot apply_plan(const WorkspaceEditPlan& plan,std::stop_token cancel={}) const;
    // Existing ancestor, absent final entry. Optional parent creation binds each
    // missing directory. Planning has no effects; application needs a durable claim.
    WorkspaceCreatePlan plan_creation(const std::string& path,const std::string& content,std::stop_token cancel={},bool create_parents=false) const;
    // Existing ancestor guidance for an optional missing-parent creation. The
    // actual proposal separately binds that ancestor and every absent directory.
    std::string creation_directory(const std::string& path,std::stop_token cancel={}) const;
    // Handle-relative create-new never overwrites an existing entry. Creation
    // itself is an effect, including an empty file; subsequent failures are uncertain.
    WorkspaceSnapshot apply_creation(const WorkspaceCreatePlan& plan,std::stop_token cancel={}) const;
    // Reconciliation inspection hashes bounded raw bytes, including a partial
    // write that is no longer valid UTF-8. It never exposes those bytes to models.
    WorkspaceFingerprint fingerprint_file(const std::string& path,std::stop_token cancel={}) const;
    // Handle-relative reconciliation of an optional workspace entry. Missing
    // entries are reported only after verifying the opened root and every
    // existing ancestor; present entries are fingerprinted as bounded raw bytes.
    WorkspacePathFingerprint fingerprint_optional_file(const std::string& path,std::stop_token cancel={}) const;
    WorkspaceListing list_files(const std::string& path=".",std::stop_token cancel={}) const;
    WorkspaceSearch search_files(const std::string& query,std::stop_token cancel={}) const;
    WorkspaceSearch search_files(const std::string& query,const WorkspaceSearchOptions& options,std::stop_token cancel={}) const;
    WorkspaceGlob glob_files(const std::string& pattern,const std::string& path=".",
        bool hidden=false,std::size_t limit=100,std::stop_token cancel={},bool respect_ignore=true) const;
    WorkspaceFind find_files(const std::string& query,const std::string& path=".",
        std::size_t limit=100,std::stop_token cancel={},bool hidden=false,bool respect_ignore=true,std::string type="file") const;
    static std::vector<ModelToolDefinition> definitions();
    // Validate exact built-in argument shapes and return a bounded JSON result.
    std::string invoke(const std::string& name,const std::string& arguments_json,std::stop_token cancel={}) const;
private:
    WorkspaceGlob discover_files(const std::string& pattern,const std::string& path,bool hidden,
        std::size_t limit,std::stop_token cancel,bool respect_ignore,
        const std::function<bool(const std::string&,bool)>& visitor={}) const;
    WorkspaceSnapshot read_snapshot(const std::string& path,bool capture_version,std::stop_token cancel,bool require_text=true,std::size_t max_bytes=1024*1024,std::size_t* bytes_read=nullptr) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct WorkspaceToolFolder {std::string folder_id,root;};
struct WorkspaceMembershipSnapshot {std::string workspace_id;std::int64_t revision=0;std::vector<WorkspaceToolFolder> folders;};
// A client may identify a workspace file, but never supplies its contents.
// The backend snapshots the file from the admitted workspace before persisting
// the prompt, so clients cannot attach data from outside that authority.
struct PromptFileAttachment {std::string path,folder_id,name,description,mention;};
// Immutable membership snapshot for one admitted tool catalogue. Each member
// is independently opened and identity-checked; folder IDs never become paths.
class WorkspaceToolSet {
public:
    WorkspaceToolSet(std::string workspace_id,std::int64_t revision,std::vector<WorkspaceToolFolder> folders);
    WorkspaceToolSet(const WorkspaceToolSet&)=delete;
    WorkspaceToolSet& operator=(const WorkspaceToolSet&)=delete;
    const std::string& workspace_id()const{return workspace_id_;}
    std::int64_t revision()const{return revision_;}
    const std::vector<WorkspaceToolFolder>& folders()const{return folders_;}
    std::size_t folder_index(const std::string& folder_id)const;
    WorkspaceTools& folder_tools(std::size_t index)const;
    std::vector<ModelToolDefinition> definitions()const;
    std::string invoke(const std::string& name,const std::string& arguments_json,std::stop_token cancel={})const;
private:
    std::string workspace_id_;
    std::int64_t revision_=0;
    std::vector<WorkspaceToolFolder> folders_;
    std::vector<std::unique_ptr<WorkspaceTools>> tools_;
};
}
