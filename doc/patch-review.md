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

The installed VS Code client remains the separately verified 0.1.3 recovery
package. This patch client change has not been installed. The native patch
candidate now includes model/guidance, CLI and graph integration in source,
and still requires a passing complete gate and real-provider approval acceptance. The earlier
99-contract hosted gate failed; see [native implementation](native-file-patch.md).
