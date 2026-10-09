# Approved file creation with missing parent folders

The native `create_file` tool accepts optional `create_parents: true`. It can
then create missing parent folders and the new UTF-8 file under one durable
approval. The default still requires an existing parent. Existing files are
never overwritten. This closes a basic project-generation gap without requiring
a configured shell executable or moving filesystem execution into a UI adapter.

The pinned OpenCode reference is commit
`3a103fe0aff726a4edc7492f03f7b88195d9e4c9` (`v2.0.16`), whose
[`write` tool](https://github.com/anomalyco/opencode/blob/3a103fe0aff726a4edc7492f03f7b88195d9e4c9/packages/core/src/tool/plugin/write.ts)
describes automatic missing-parent creation. It is a behavior reference; its
implementation is not embedded or executed. xMind's create-new operation
remains separate from literal editing and does not establish complete write or
patch parity.

Planning opens and verifies the nearest existing ancestor, captures its identity
and lists every absent directory. It creates nothing. Paths must remain within
the authorized workspace, with valid components and at most 32 parent folders.
Directory links and backend-private paths remain outside tool authority.

The agent first delivers applicable repository and skill guidance for the
existing ancestor. The approval binds that guidance, the ancestor identity,
the ordered missing folders, file absence and exact content. The shared
browser/VS Code approval card lists the folders as escaped text beside the
file proposal. A newly present folder invalidates the proposal before effects,
including when another writer adds an `AGENTS.md` there.

After the native operation claims its grant, C++ creates each missing directory
and the final file using handle-relative Windows create-new calls. Existing
entries are never opened for overwrite. Successful receipts attribute the
created folders and verified final bytes. The database journal still uses
embedded xlang3 SQLite.

Directory/file creation is not an atomic transaction across the filesystem.
After any creation, an unexpected failure or cancellation is uncertain. The
backend preserves the claim/outcome for reconciliation, does not delete partial
results and does not replay the operation. The tests cover a journal-write
failure after actual directory/file creation and quarantine on database reopen;
they do not inject every failure between intermediate directory effects.

Native executor and HTTP/CLI contracts verify approved nested creation,
denial/cancellation without folders, stale directory/guidance rejection,
changed-plan rejection, exact ancestor guidance delivery and journal failure
with actual retained directories and file bytes. Their inference is synthetic.
The first focused HTTP attempt reached its old total-request assertion after
the new scenario passed; the fixture was corrected to include that scenario's
three individually checked calls. The original failure remains retained.

A separate real OpenAI `gpt-6.1-sol` check passed four runs: create two absent
folders and a file after approval, approve an edit, deny a further edit while
preserving the file, and deny a new nested creation while preserving folder
absence. Each run continued through the actual provider and recorded two
supplied usage events. The adapter did not write the tested files, read provider
keys or execute CPython. The original live source map is retained; its only
difference from the final gate is the CI helper's expected test count.
[Live receipts and usage](evidence/live-native-cli-parent-creation.json).

The CI view helper's stale expected count was corrected from 198 to 200. The
guarded local adaptation changes only the CI-only guard and evidence path;
it does not spoof a GitHub runner. The final source gate and exact native
contract manifest are recorded in
[local evidence](evidence/native-create-parents-local.json) and
[complete native output](evidence/native-create-parents-local-ctest.log).
The final gate passed **98 native contracts in 203.59 seconds**, with all 631
mapped inputs unchanged and exact expected/registered/passed names matched.
All **200 extension and 39 browser tests** passed with 41 view inputs and
11 browser assets unchanged. The SDK remains `7b8b32ae` with its CPython bridge
disabled. The earlier 98-contract pass before the CI count correction took
205.71 seconds; its artifacts remain separate from the final gate.
The earlier checkpoint's observed hosted view failure is retained separately;
its logs have not been inspected, so no exact hosted failure cause is claimed.

These changes are source-built native and isolated client acceptance. The
installed VS Code package and pending migration target were not replaced.
Rendered folder approvals, installed writing, general multi-file patch review
and the rest of the pinned coding parity remain incomplete.
