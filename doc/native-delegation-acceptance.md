# Native delegation acceptance scopes

The current managed preview uses the later verified 7fe diagnostic backend with
the same repaired browser view. Its new public failure identifies only an
encrypted-content difference. [Current installation and diagnostic scope](native-responses-diagnostics.md).
Successful live delegation remains unverified; the compatibility source is
[under validation](native-responses-reasoning.md).

The historical acceptance below used the exact hosted e353 backend and schema
v10. Its browser view contains a separate local source repair. Successful live
delegation is still unverified: the first real `delegate_tasks` response failed
terminal consistency validation before any child was admitted.

These are separate evidence scopes; the browser repair does not change the
recorded hosted gate or establish a new native build.

The public records were observed on 2026-10-08 UTC: managed upgrade at
11:13:19.302, startup failure at 11:18:32.278, view-only repair at
11:22:03.588, and refreshed DOM/live-failure metadata at 11:27:38.465.
These are observation times, not measured test or execution durations.

| Scope | Actual result | Evidence |
| --- | --- | --- |
| Frozen local native implementation | 71/71 native in 142.36 s; 103 extension in 1.7564936 s; 19 browser in 0.894282 s; native/browser integration and 18 VSIX assets passed | [Local source/hash/manifest provenance](evidence/native-delegation-local-provenance.json) |
| Exact hosted `e353a37799530a234a6fa13e51f61a5c52d3ae6a` | 71/71 native in 175.51 s; 103 extension in 2.9668103 s; 19 browser in 1.426115 s; all 16 job steps, native/browser integration and 18 VSIX assets passed | [Exact hosted revision/artifact/pins](evidence/native-delegation-hosted-provenance.json), [raw hosted CTest](evidence/native-delegation-hosted-ctest.log) |
| Disposable upgrade/rollback fixture | Actual hosted native/xlang3 schema 9→10→9, retained schema 10; real native read and controller-approved create, encrypted synthetic credential preservation, access-cookie reuse and no persisted-work replay. No provider inference/discovery requested | [Upgrade provenance and separated fixture/precheck scopes](evidence/native-delegation-upgrade-provenance.json) |
| Actual managed backend upgrade | Exact hosted e353 installed with schema v10; prior 11 sessions, 15 roots, 37 root-history rows, three operation journals, two graphs and two graph children preserved, with settings and access. Prior state contained no delegated leaves | [Actual upgrade scope](evidence/native-delegation-upgrade-provenance.json) |
| Original e353 rendered webpage | Failed startup: duplicate classic-script `observeOwnedRun`; the missing browser bridge then caused `acquireVsCodeApi` failure | [Actual startup errors](evidence/native-delegation-browser-startup-failure.json) |
| Separate local browser repair | 103 extension in 1.6557025 s; 20 browser in 0.9090882 s; zero failures/skips, actual native/browser fixture and all 18 VSIX assets passed. Ten frozen view sources and 12 installed view files recorded; native bundle preserved | [Repair provenance](evidence/native-delegation-browser-global-fix-provenance.json), [extension log](evidence/native-delegation-browser-global-extension.log), [browser log](evidence/native-delegation-browser-global-browser.log), [native/browser log](evidence/native-delegation-browser-global-native.log), [VSIX log](evidence/native-delegation-browser-global-package.log) |
| Repaired rendered webpage | Existing cookie reused without key entry; prior public README response/history/metrics restored, bottom `gpt-5.6-sol` model and Agent default selected. An immediate refresh retained connection and selection, then displayed the same failed live run | [Refreshed actual DOM observation](evidence/native-delegation-repaired-browser-dom.json), [actual screenshot](evidence/native-delegation-browser-live-failure.jpg) |
| First live delegation request | Failed `model_protocol_error` / `responses_terminal_mismatch` after streamed `delegate_tasks` arguments; zero children, only the user history row and one parent model-budget attempt | [Filtered live failure record](evidence/native-delegation-first-live-failure.json) |
| Installed VS Code delegation and successful live join | Not accepted | Remain required |

The repair wraps browser-local declarations so production classic scripts share
one global realm safely. The new regression executes the emitted production
HTML's client/browser/renderer script order, installs the real browser bridge,
and restores isolated child history and supplied metrics. Those regression
backend replies are explicitly synthetic. The original duplicate-declaration
failure was reproduced before the fix. Packaging and the actual rendered check
are separate from that regression.

The failed public live root is `20349b91c30e11b54f90902e137899da`, in session
`33b0cae2051699d925364ce1e986c55c`. The parser rejected completed-item versus
terminal-output consistency before a tool invocation or delegation admission.
The differing field remains unconfirmed; the next native diagnostic and
independent terminal-snapshot fixture must identify it before any guard changes.
The live failure record contains 109 tool deltas, zero accepted tool calls and
no validated provider usage. The rejected response has no invented usage or
assistant metrics. This failure does not prove successful leaf execution,
join, approval, coding effect or live result continuation. Reloading the view
did not resubmit it.

The refresh evidence covers the actual immediate reloads that were exercised;
it does not establish an eight-hour acceptance window. Earlier c4 browser/CLI
acceptance remains [historical](preview-checkpoint-c4ec09fc.md), with its own
unresolved original reconnect cause. Private records, credentials and their
fingerprints are excluded from these public evidence files.

The implementation boundary is [bounded native delegation](native-delegation.md).
Model-selected dependency DAGs, human checks, revisioned replanning, broader
authorized child presets, skills, compaction and outbound A2A remain required.
The [next native planning architecture](native-dynamic-plan-design.md) is a
proposal, not implementation or completion evidence.
