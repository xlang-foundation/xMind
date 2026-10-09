# Patch review in the shared clients

The native per-file `patch_file` proposal is now understood by the shared sidebar,
VS Code comparison provider and browser controller. Review identifies additions,
edits, deletions and moves, including move destinations, absent before/after
files, missing parent folders and the requested batch manifest. Each file has its
own approval. The view explains that a failed or denied later file leaves earlier
completed changes in place.

Comparison uses only revalidated backend snapshots. VS Code displays read-only
`xmind-review` documents; the browser displays text snapshots with explicit
absence labels. Opening either comparison grants no permission and reads no
workspace file. Both adapters recheck operation ownership, state and exact
proposal bytes before review. Malformed patch snapshots disable Allow and
comparison, retain Deny, and reject a forged Allow message in the host/controller.
Native still owns path authorization, snapshot preconditions and effects.

The initial local candidate passed all **221 extension and 41 browser contracts**, with
44 source/vendor input hashes and 12 generated asset hashes unchanged throughout
the complete suites. Tests use labeled synthetic proposals, including every
action, stale review, malformed absence/manifest and forged Allow cases. This is
adapter/DOM acceptance, not a native patch execution or rendered live approval.
[Frozen local evidence](evidence/patch-review-local.json).

Source/destination guidance scopes are now displayed separately for move
proposals, with both sets of source bindings included. Malformed scope metadata
disables Allow. This follow-up passed the complete frozen **222/41** adapter
gate with 44 input and 12 asset hashes unchanged.
[Guidance follow-up evidence](evidence/patch-guidance-local.json).

The hosted client workflow for exact revision
`6083a62dbe3354b528e3822c9a4222c7d9c21de3` also passed **222/41** tests.
The downloaded artifact's advertised digest and bytes were verified, all 42
tracked sources were bound to that commit, and 12 generated assets were checked
against the archive and unchanged gate maps. This is client acceptance only.
[Independently verified hosted evidence](evidence/patch-guidance-hosted.json).

The 0.1.4 client is now installed with the separately audited native patch
runtime. Actual TestProj review displayed a real-model two-file addition,
opened its proposed comparison while the folder remained absent and required
two separate approvals. Both actual creations and receipts matched independent
file hashes; the completed run retained real token metrics.
[Installed evidence and limits](evidence/vscode-patch-installed.json).
Mixed update/move/delete and partial denial were accepted separately through
the packaged CLI. Installed/browser coverage for those review cases remains
open, as does compact composer reset after long requests.
[Native implementation and broader scope](native-file-patch.md).
