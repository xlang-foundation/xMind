# Native multi-folder workspaces

## Status

This is the target contract for browser, CLI and VS Code. The local xlang3
SQLite registry now persists a workspace's primary folder, member-folder
identities, ordering and membership revision, and the native local-view
catalogue returns that metadata. In the local browser, **New workspace** starts
with one folder and **Add folder** attaches more folders to the same workspace;
the selected workspace and browser session stay in place. The C++ registry
contract covers creation, folder attachment, reopen, private-root/overlap
rejection and concurrent membership changes. A managed local backend now reads
the registry transactionally at each single-agent root admission. Its native
read/list/search/glob tools accept an optional registered `folder_id`, default
to the primary folder, reopen each admitted root and verify its identity. The
run stores its exact membership revision and folder bindings; admitted agent
children reuse that snapshot. New graph roots also persist membership atomically
with graph admission; direct graph read nodes and graph-agent children use that
same immutable set. Single-agent `edit_file` and `create_file` calls now accept
the same optional `folder_id`; the proposed operation binds the workspace ID,
folder ID, membership revision and selected root identity. Server-side uncertain
edit recovery reloads the admitted membership snapshot before inspecting the
selected file. Focused Release contracts passed for workspace tools, agent
runner, graph runner, create executor and edit executor (5/5); the attached
edit contract approved a real filesystem change in the secondary folder and
verified the primary copy stayed unchanged. This proves the native contract
with local fixtures, not rendered multi-folder client acceptance. VS Code can explicitly attach another folder
already open in its window through **xMind: Add Opened Folder to Workspace**;
the native host retains the existing primary identity. The CLI can initialize
the selected folder as a workspace and attach additional folders to that same
workspace. Graph write nodes, `apply_patch`, and process tools use the admitted
folder snapshot. Git/language-server routing and folder removal are separately
validated against membership. The local browser displays each attached
folder as its own tree and reads previews through a native route that resolves
the selected workspace's folder ID, re-opens the directory and verifies its
native identity on every request.
See the [current browser behavior and gap](browser-view.md#multi-folder-workspace-target).
The browser launcher rebuilds the generated asset snapshot from the current
source at startup, keeping the rendered workspace actions aligned with the
checked-out client code.

## User workflow

1. **Create workspace** chooses an existing local folder. That folder becomes
   the first member and the primary folder. The workspace name initially comes
   from the folder name and can be renamed without renaming the directory.
2. **Add folder** attaches another existing folder to the selected workspace.
   It does not create another workspace and does not move or copy files.
3. The workspace selector switches between named workspaces. Inside the
   selected workspace, the folder list shows each member and which one is
   primary. Adding or removing a member changes the workspace membership
   revision.
4. VS Code's **Add Opened Folder to Workspace** command attaches a folder from
   `workspace.workspaceFolders` to its current native profile. It does not
   create a `.code-workspace` file or change the primary root. Removing a folder detaches it from the workspace; it never deletes the
   directory. Existing runs retain the exact membership snapshot admitted at
   their start. New runs use the new revision.

The local browser's picker can choose only paths on the xMind host. Browser
creation/attachment, CLI commands and the explicit VS Code attachment command
persist in the same profile registry. VS Code still starts from one selected
opened root; other opened folders become members only when the user invokes the
command. The CLI's `workspace init` uses `--workspace DIR` (or the current
directory) as the first folder. `workspace add-folder FOLDER` attaches another
folder to the workspace selected by `--workspace DIR`; `workspace info` shows
its primary folder and members. A future Nexus connection can supply a
server-authorized workspace record through the same protocol; it cannot turn a
client path into server authority.

## Native authority and identity

A workspace has a stable opaque ID, a name, a monotonically increasing
membership revision, a primary folder ID and an ordered set of member folders.
Each folder has its own opaque ID, display name, canonical root path and
opened-directory identity. IDs are data, never filesystem paths. The native
host canonicalizes and opens every folder and rejects inaccessible roots,
reparse/alias escapes, duplicate identities, overlapping roots and roots inside
private xMind profile/configuration storage.

The execution authority is the workspace ID plus the membership revision and
the ordered folder identities. Each admitted run receives an immutable handle
set for that revision. Workspace and folder IDs in tool calls must resolve
against that captured set. An approval records the workspace revision and
folder ID, so changing membership cannot redirect an approved edit or process
to another folder. Session, run, plan, graph and operation records retain the
workspace ID and admission revision.

Workspace membership and identity metadata live in local xlang3-managed SQLite
storage. API responses expose member IDs and display paths but never local
browser credentials or provider keys. The membership record does not grant
access by path spelling alone: execution still traverses each opened directory
handle and revalidates the handle identity before effects.

## Tool and client contract

Single-agent and graph read/list/search/glob tools and the `edit_file`/
`create_file` write tools take an optional `folder_id` and a path relative to
that folder. If `folder_id` is omitted, they resolve to the primary folder.
Results identify the folder alongside the relative path; this keeps identical
names in separate roots unambiguous. A write approval is bound to the captured
workspace revision, selected folder and root identity. `apply_patch` also
accepts an optional `folder_id` and applies its complete batch within that one
member folder; a single patch batch cannot span multiple roots. Backend-private
directories remain denied inside every root.

`run_process` uses the primary folder as its default working directory. A
different folder must be selected by folder ID and the actual working directory
must remain beneath its opened handle. When a process profile requires approval,
the proposal binds that folder and captured workspace revision. An explicitly
trusted profile can durably authorize its own process operations without a
per-operation prompt. The user-configured trust decision is scoped to that
registered local profile and remains visible as the operation's decision actor.
Git status, initialization, branch metadata and diffs can target one admitted
folder per request because one workspace may contain several independent
repositories. The native HTTP API accepts a registered `folder_id`; it resolves
that ID against a fresh immutable membership snapshot and never accepts a root
path. Omitting the ID selects the primary folder. Language servers are
started with the folder root that owns the document. Repository instructions
and skills are discovered per root and retain their source-folder provenance.
Delegation, single-agent runs, graph read/write nodes, graph-agent children, MCP
and A2A inherit only the admitted workspace snapshot; they cannot expand
membership themselves. `apply_patch` is scoped to one selected member per call.
Graph `run_process` nodes can also select a registered folder from that
immutable snapshot. Graph `edit_file`, `create_file` and each patch-file effect
retain per-operation controller approval, bound to the selected folder and
workspace revision.

The browser canvas shows one file tree per member folder, with explicit folder
selection for previews. VS Code's Git initialize/status/review commands now
offer registered folders from the backend membership; the CLI Git commands
accept a `FOLDER_ID` (or `--folder-id ID` for `git-diff`). The browser Git
panel lists registered folders and routes initialization, status, base
detection and diffs through the selected folder ID. VS Code can
attach opened folders; single-agent read/list/search/glob/edit/create/patch and
process tools use the captured registry membership. Direct graph reads,
graph-agent children and graph read/write/patch/process nodes use that same
snapshot. A single patch call selects one member folder; it cannot span roots.
Terminals remain bound to the primary folder. VS Code diagnostics, navigation
and formatting select the registered root containing each document; native
requests validate membership and return the selected folder ID. Model-visible
`language_server` calls may select only a folder from the run's captured
membership snapshot; omission selects the primary root. The focused native
contract verifies that the selected folder's symbols are returned and an
unadmitted folder is rejected before dispatch. Full multi-folder effect
acceptance remains open.

## Required acceptance

- Create a workspace from one real folder, then attach a second folder with
  colliding relative filenames; prove the native identities and reads stay
  distinct in browser, CLI and VS Code.
- Read, search, edit-preview and approve an edit in each folder. Verify an
  escaped path, an unregistered folder ID and a removed folder ID are rejected
  before filesystem effects.
- Run a process in each folder. Verify default cwd is the primary folder,
  explicit cwd is pinned to the selected folder, and approval rejects changed
  membership or executable identity.
- Show independent Git state for two repositories through native HTTP, CLI and
  VS Code; expose and verify browser folder selection. The native HTTP, CLI,
  VS Code host and browser folder-routing contracts now cover these paths; a
  live two-repository editor/browser acceptance run remains open. Verify
  language-server roots for documents in each folder; real native Rust Analyzer
  and VS Code routing contracts now cover this path.
- Add a folder while a run is active. Prove the admitted run remains pinned to
  its old revision and a later run sees the new revision across model-visible
  language-server calls as well as workspace tools.
- Switch named workspaces in the browser and VS Code. Prove sessions/history
  are scoped to the workspace, late events do not cross the switch, and a
  failed switch preserves the previously selected workspace without a stale
  label.
- Restart the managed backend and verify the workspace name, root ordering,
  primary folder, membership revision, sessions and provider credentials reopen
  from local SQLite without replaying work or exposing secrets.

Focused contracts do not establish the complete multi-folder acceptance above.

- Initialize a CLI workspace from its first folder, attach a second folder, and
  verify the workspace ID and primary-folder identity stay stable while the
  membership revision increments. Duplicate and overlapping roots must be
  rejected without changing membership.
